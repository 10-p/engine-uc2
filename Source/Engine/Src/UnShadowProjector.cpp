/*=============================================================================
	UnShaderProjector.cpp: Projected shadow rendering code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UShadowBitmapMaterial);

// scion sz -
#define NEW_SHADOW_BLEND

//
//	UShadowBitmapMaterial::Destroy
//

void UShadowBitmapMaterial::Destroy()
{
	guard(UShadowBitmapMaterial::Destroy);

	FAuxRenderTarget*&	ShadowTexture = *(FAuxRenderTarget**)&TextureInterfaces[0];
	FAuxRenderTarget*&	TempRenderTarget = *(FAuxRenderTarget**)&TextureInterfaces[1];

	if(ShadowTexture)
	{
		delete ShadowTexture;
		ShadowTexture = NULL;
	}

	if(TempRenderTarget)
	{
		delete TempRenderTarget;
		TempRenderTarget = NULL;
	}

	Super::Destroy();

	unguard;
}

//
//	UShadowBitmapMaterial::GetRenderInterface
//

FBaseTexture* UShadowBitmapMaterial::GetRenderInterface()
{
	static FSolidColorTexture	InvalidShadowTexture(FColor(127,127,127));

	if(Invalid)
		return &InvalidShadowTexture;
	else
		return *(FBaseTexture**)&TextureInterfaces[0];
}

//
//	FShadowBitmapRenderInterface
//

class FShadowBitmapRenderInterface : public FRenderInterface
{
public:

	BYTE				Darkness;
	FRenderInterface*	RI;

	// Constructor/destructor.

	FShadowBitmapRenderInterface(FRenderInterface* InRI,BYTE InDarkness)
	{
		RI = InRI;
		Darkness = InDarkness;

		RI->PushState();
		RI->EnableLighting(0,0);
		RI->SetCullMode( CM_CCW );
	}

	~FShadowBitmapRenderInterface()
	{
		RI->PopState();
	}

	// FRenderInterface interface.

	virtual void PushState()
	{
		RI->PushState();
	}

	virtual void PopState()
	{
		RI->PopState();
	}

	virtual UBOOL SetRenderTarget(FRenderTarget* RenderTarget)
	{
		return RI->SetRenderTarget(RenderTarget);
	}
	//scion jg -- Added for restoring previous state without Push/PopState()
	virtual void ResetRenderTarget(void)
	{
		RI->ResetRenderTarget();
	}

	virtual FRenderTarget* GetRenderTarget()
	{
		return RI->GetRenderTarget();
	}
	//scion jg -- Used to determine if we are back buffer rendering or not
	// which we never are with a shader render interface
	virtual UBOOL IsDefaultRenderTarget(void) { return 0; }


	virtual void SetViewport(INT X,INT Y,INT Width,INT Height)
	{
		RI->SetViewport(X,Y,Width,Height);
	}

	// scion sz -
	virtual void SetScreenSpaceOffset( FLOAT OffsetX, FLOAT OffsetY )
	{
		RI->SetScreenSpaceOffset( OffsetX, OffsetY );
	}

	//scion jg -- Added for resetting to previous state
	virtual void ResetViewport(void)
	{
		RI->ResetViewport();
	}

	virtual void GetViewport(INT* X,INT* Y,INT* Width,INT* Height)
	{
		RI->GetViewport(X,Y,Width,Height);
	}

	virtual void Clear(UBOOL UseColor,FColor Color,UBOOL UseDepth,FLOAT Depth,UBOOL UseStencil,DWORD Stencil)
	{
		RI->Clear(UseColor,Color,UseDepth,Depth,UseStencil,Stencil);
	}

	virtual void PushHit(const BYTE* Data,INT Count)
	{
		RI->PushHit(Data,Count);
	}

	virtual void PopHit(INT Count,UBOOL Force)
	{
		RI->PopHit(Count,Force);
	}

	virtual void SetCullMode(ECullMode CullMode)
	{
	}

	virtual ECullMode GetCullMode() 
	{ 
		return CM_None; 
	}

	virtual void SetAmbientLight(FColor Color)
	{
	}

	virtual void EnableLighting(UBOOL UseDynamic,UBOOL UseStatic,UBOOL Modulate2X,FBaseTexture* LightMap,UBOOL LightingOnly,const FSphere& LitSphere)
	{
	}

	virtual void SetLight(INT LightIndex,FDynamicLight* Light,FLOAT scale)
	{
	}

	virtual void SetNPatchTesselation( FLOAT Tesselation )
	{
	}

	virtual void SetDistanceFog(UBOOL Enable,FLOAT FogStart,FLOAT FogEnd,FColor Color)
	{
	}

	virtual void SetGlobalColor(FColor Color)
	{
	}

	virtual void SetTransform(ETransformType Type,const FMatrix& Matrix)
	{
		RI->SetTransform(Type,Matrix);
	}

	virtual void SetZBias(INT ZBias)
	{
		RI->SetZBias(ZBias);
	}

	virtual void SetStencilOp(ECompareFunction Test,DWORD Ref,DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,DWORD WriteMask)
	{
		RI->SetStencilOp(Test,Ref,Mask,FailOp,ZFailOp,PassOp,WriteMask);
	}
	virtual void ResetStencilOp(void)
	{
		RI->ResetStencilOp();
	}

	virtual void EnableStencil(UBOOL bEnable)
	{
		RI->EnableStencil(bEnable);
	}

	virtual void EnableDepth(UBOOL bEnable)
	{
		RI->EnableDepth(bEnable);
	}

	virtual void SetPrecacheMode( EPrecacheMode PrecacheMode )
	{
		RI->SetPrecacheMode( PrecacheMode );
	}

	virtual INT SetMaterial(UMaterial* Material,FString* ErrorString,UMaterial** ErrorMaterial,INT* NumPasses)
	{
		DECLARE_STATIC_UOBJECT(
			UConstantColor,
			ShadowColor,
			{
			}
			);

			// scion sz - added in order to disable ztest/zwrite (faster)
		DECLARE_STATIC_UOBJECT(
			UFinalBlend,
			ShadowCastBlend,
			{
				ShadowCastBlend->FrameBufferBlending = FB_Overwrite;				
				ShadowCastBlend->ZWrite = 0;
				ShadowCastBlend->ZTest = 0;
				ShadowCastBlend->Material = ShadowColor;
			}
			);

#if defined(_XBOX) && defined(NEW_SHADOW_BLEND)
		ShadowColor->Color = FColor(Darkness,Darkness,Darkness,255);		
#else
#if defined(NEW_SHADOW_BLEND)
		// UC2 port (M7b): the Xbox branch above, chosen at run time by a device with the
		// XboxShadows capability (UnRenDev.h): the caster is drawn in ShadowDarkness grey, its
		// opacity ignored, as on Xbox. The blend's material is set back to the constant colour
		// because the PC branch below may have left its shader there.
		if( RenDevHasXboxShadows() )
		{
			ShadowColor->Color = FColor(Darkness,Darkness,Darkness,255);
			ShadowCastBlend->Material = ShadowColor;
		}
		else
#endif
		{
		UShader*	Shader = Cast<UShader>(Material);
		ShadowColor->Color = FColor(127,127,127,255 - Darkness);
		if(Shader)
		{
			DECLARE_STATIC_UOBJECT(
			UShader,
			ShadowShader,
			{
			}
			);

			ShadowShader->Diffuse = ShadowColor;
			ShadowShader->Opacity = Shader->Opacity;

			ShadowCastBlend->Material = ShadowShader;
		}
		}
#endif // defined(_XBOX) && defined(NEW_SHADOW_BLEND)		

		return RI->SetMaterial(ShadowCastBlend);
	}

	virtual void AddVertexShaderEnum( QWORD VertexShaderEnum )
	{
        RI->AddVertexShaderEnum( VertexShaderEnum );
	}

	virtual INT SetVertexStreams(QWORD VertexShaderEnum,FVertexStream** Streams,INT NumStreams)
	{
		return RI->SetVertexStreams(VertexShaderEnum,Streams,NumStreams);
	}

	virtual INT SetDynamicStream(QWORD VertexShaderEnum,FVertexStream* Stream)
	{
		return RI->SetDynamicStream(VertexShaderEnum,Stream);
	}

	virtual INT SetIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseIndex)
	{
		return RI->SetIndexBuffer(IndexBuffer,BaseIndex);
	}

	virtual INT SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseIndex)
	{
		return RI->SetDynamicIndexBuffer(IndexBuffer,BaseIndex);
	}

	virtual void DrawPrimitive(EPrimitiveType PrimitiveType,INT FirstIndex,INT NumPrimitives,INT MinIndex = INDEX_NONE,INT MaxIndex = INDEX_NONE)
	{
		RI->DrawPrimitive(PrimitiveType,FirstIndex,NumPrimitives,MinIndex,MaxIndex);
	}

	virtual DWORD GetMaterialVertexShaderType(UMaterial* Material) 
	{ 
		return 0; 
	}

	virtual void SetSkinBones( INT FirstBoneDestIndex, FCoords* FirstBone, INT BoneCount ) 
	{
        RI->SetSkinBones( FirstBoneDestIndex, FirstBone, BoneCount );
	}

#ifdef _XBOX
	// scion sz - passthrough functionality to FD3DRenderInterface

	virtual void DrawPrimitiveUP(QWORD VertexShaderEnum,EPrimitiveType PrimitiveType, INT NumPrimitives,FVertexStream* pVBStream,FIndexBuffer* pIBStream = NULL) 
	{
		RI->DrawPrimitiveUP( VertexShaderEnum, PrimitiveType, NumPrimitives, pVBStream, pIBStream );
	}

	virtual void RunPushBuffer(FPushBuffer* pPushBuffer,UBOOL bUseIndexStream = 0,UBOOL bCommit = 1)
	{
		RI->RunPushBuffer( pPushBuffer, bUseIndexStream, bCommit );
	}
	
	virtual DWORD* BeginPush(DWORD dwSizeNeeded,UBOOL bCommitStates = 1, UBOOL bSetTexCoords = 1)
	{
        return RI->BeginPush( dwSizeNeeded, bCommitStates, bSetTexCoords );
	}
	
	virtual DWORD* BeginPush(DWORD dwSizeNeeded,QWORD VertexShaderEnum,	FVertexStream* pVBStream,UBOOL bCommitStates = 1,UBOOL bSetTexCoords = 1)
	{
		return RI->BeginPush( dwSizeNeeded, VertexShaderEnum,	pVBStream, bCommitStates, bSetTexCoords );
	}

	virtual void EndPush(DWORD* pEndPointer)
	{
		RI->EndPush( pEndPointer );
	}

	virtual DWORD InsertFence(void)
	{
		return RI->InsertFence();
	}
#endif
	//scion jg -- Allows the direct setting of a renderstate bypassing the deferred commit
	virtual void SetRenderStateDirect(BYTE State,DWORD Value)
	{
		RI->SetRenderStateDirect(State,Value);
	}
	//jg -- Occlusion query support
	virtual UBOOL BeginOcclusionQuery(void)
	{
		return RI->BeginOcclusionQuery();
	}
	virtual UBOOL EndOcclusionQuery(DWORD QueryNum)
	{
		return RI->EndOcclusionQuery(QueryNum);
	}
	virtual UBOOL GetOcclusionQueryResults(DWORD QueryNum,DWORD& dwPixels)
	{
		return RI->GetOcclusionQueryResults(QueryNum,dwPixels);
	}
	//jg -- So we can set vertex shader constants
	virtual void SetVertexShaderConstant(INT Register,const void* pConstantData,DWORD ConstantCount)
	{
		RI->SetVertexShaderConstant(Register,pConstantData,ConstantCount);
	}
};

//
//  FShadowSceneNode
//
class ENGINE_API FShadowSceneNode : public FActorSceneNode
{
public:

	// Constructor.
    FShadowSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget,AActor* InActor,AActor* CameraActor,FVector CameraLocation,FRotator CameraRotation,FLOAT CameraFOV) : FActorSceneNode(InViewport,InRenderTarget,InActor,CameraActor,CameraLocation,CameraRotation,CameraFOV)
    {
    }

    virtual UBOOL FilterAttachment(AActor* AttachedActor)
    {
		if( AttachedActor && 
		(!AttachedActor->bActorShadows || AttachedActor->bDeleteMe) ) {
            	return 0;
		}

		// scion sz - never draw post process items as casting geometry
		FDynamicActor* DynActor = AttachedActor->GetActorRenderData();
		if( DynActor->IsPostProcess != POSTPROCESS_NONE ) {
			return 0;
		}

        return FActorSceneNode::FilterAttachment(AttachedActor);
    }

	virtual FSceneNode* GetLodSceneNode()
	{
		if(Viewport->LodSceneNode)
			return Viewport->LodSceneNode;
		else
			return this;
	}

	virtual UBOOL IsShadowSceneNode()
	{
		return 1;
	}
};

/* scion ======================================================================
 * UShadowBitmapMaterial::ClearShadowBitmap
 * Author: jg
 * 
 * On the xbox this clears the shadow bitmap by drawing to it. On the PC it
 * uses RI->Clear(). This is needed because you cannot take any depth/stencil
 * buffer actions on an L8 render target. If you do it generates GHEs.
 *
 * input:	Target -- The bitmap to clear
 *			RI -- The render interface to draw with
 *			ClearColor -- the color to clear to
 *
 * ============================================================================
 */
void UShadowBitmapMaterial::ClearShadowBitmap(FRenderTarget* Target,
	FRenderInterface* RI,FColor ClearColor)
{
	guard(UShadowBitmapMaterial::ClearShadowBitmap);
#ifndef _XBOX
	// Not clearing depth just color. Don't clear stencil either
	RI->Clear(1,ClearColor,0,1.0f,0,0xFF);
#else
	DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,ShadowClearBM,
	{
		// Set the color to clear to
		static FSolidColorTexture ClearColorTex(FColor(255,255,255,255));
		// Update the texture interface for this call
		ShadowClearBM->SetTextureInterface(&ClearColorTex);
	});
	DECLARE_STATIC_UOBJECT(UFinalBlend,ShadowClear,
	{
		ShadowClear->Material = ShadowClearBM;
		ShadowClear->FrameBufferBlending = FB_Overwrite;
		ShadowClear->ZWrite = 0;
		ShadowClear->ZTest = 0;
		ShadowClear->TwoSided = 1;
		// Alpha blending on L8 causes GHE
		ShadowClear->OverrideAlpha = 1;
		ShadowClear->AlphaEnable = 0;
	});
	static FColor ColorBk(0,0,0,0);
	RI->SetAmbientLight(ColorBk);
	// Draw the constant color to the render target without touching
	// the depth buffer
	FCanvasUtil(Target,RI).DrawTile(0,0,Target->GetWidth(),
		Target->GetHeight(),0,0,0,0,1,ShadowClear,
		ClearColor);
#endif
	unguard;
}

//
//	UShadowBitmapMaterial::Get
//

UBitmapMaterial* UShadowBitmapMaterial::Get(FTime Time,UViewport* Viewport)
{
	guard(UShadowBitmapMaterial::Get);

	// scion sz - scale texture resolution
	if( USize != UScaledSize ) {
		USize = UClamp = UScaledSize;
	}
	if( VSize != VScaledSize ) {
		VSize = VClamp = VScaledSize;
	}	

#ifndef _XBOX
	//scion jg -- Must be powers of 2 on PC no linear support and no L8
	// ufront: only a size the PC cannot do falls back to 256. A power of two from 32 to 1024 is kept, as the Xbox
	// kept every size: the game's own shadows ask for 256 (every player shadow is SD_Normal; an ambient creature
	// can ask for SD_Low..SD_SuperHigh, 128..1024), and a mod's shadow material asks for its own (jcbShadows'
	// resolution setting).
	if( !(USize==VSize && USize>=32 && USize<=1024 && (USize & (USize-1))==0) )
		USize = VSize = 256;
	UClamp = USize;
	VClamp = VSize;
#endif

	if( bBlobShadow || Invalid || Viewport->bForceBlobShadows )
		return BlobShadow;

	if(Dirty && ShadowActor) 
	{
		BEGIN_D3D_EVENT(BDE)(PIX_SHADOW,TEXT("%s"),ShadowActor->GetName());
		FLOAT SavedLastRenderTime = ShadowActor->LastRenderTime; 
	    
		FRenderInterface*	RI = Viewport->RI;
		FAuxRenderTarget*&	ShadowTexture = *(FAuxRenderTarget**)&TextureInterfaces[0];
		FAuxRenderTarget*&	TempRenderTarget = *(FAuxRenderTarget**)&TextureInterfaces[1];

		FVector		SavedLocation = Viewport->Actor->Location;
		FRotator	SavedRotation = Viewport->Actor->Rotation;
		INT			SavedSizeX = Viewport->SizeX,
					SavedSizeY = Viewport->SizeY;
		FLOAT		SavedScaleX = Viewport->ScaleX,
					SavedScaleY = Viewport->ScaleY;
		FLOAT		SavedScaleFOVX = Viewport->ScaleFOVX,
					SavedScaleFOVY = Viewport->ScaleFOVY;
		DWORD		SavedShowFlags = Viewport->Actor->ShowFlags;
		UBOOL		SavedCinematics = Viewport->bRenderCinematics;

        AActor*		CameraActor = Viewport->Actor;
	    FVector		CameraLocation;
	    FRotator	CameraRotation;
        FLOAT FovBias  = 1.0f;
        if( CameraActor )
        {
            CameraLocation = CameraActor->Location;
            CameraRotation = CameraActor->Rotation;
            FovBias = appTan( Viewport->Actor->FovAngle*(PI/360.f) );
            Viewport->Actor->eventPlayerCalcView( 0.f, CameraActor, CameraLocation, CameraRotation, 0 );
        }

		USkeletalMeshInstance*	MeshInstance = (ShadowActor->IsDTMesh() && ShadowActor->Mesh) ? Cast<USkeletalMeshInstance>(ShadowActor->Mesh->MeshGetInstance(ShadowActor)) : NULL;

		FVector ShadowLocation = MeshInstance ? MeshInstance->GetBoneCoords(0).Origin : ShadowActor->Location + FVector(0,0,5),
				FrustumOrigin = ShadowLocation + LightDirection * LightDistance;

		// distance fade shadows
		float ShadowAtten = 1.0f;
#if 0
		if( CameraActor && CullDistance > 0.0f )
		{
			float effectiveDist = (ShadowLocation - CameraLocation).Size() * FovBias;
			float distRatio = effectiveDist / CullDistance;
			//ShadowAtten = 1.0f-Clamp( appPow(distRatio,2.0), 0.0, 1.0);

			// if outside of cull distance just return a blob shadow
			if( distRatio >= 1.0f )
				return BlobShadow;

			// don't attenuate shadows.
			ShadowAtten=1.0f;
		}
#endif

		Viewport->Actor->Location = FrustumOrigin;
		Viewport->Actor->Rotation = (-LightDirection).Rotation();
		Viewport->SizeX = USize-4;	// scion sz - shrink viewport by 2 pixels 
		Viewport->SizeY = VSize-4;	// scion sz - shrink viewport by 2 pixels
		Viewport->ScaleX = Viewport->ScaleY = 1.0f;
		Viewport->ScaleFOVX = Viewport->ScaleFOVY = 1.0f;
		Viewport->bRenderCinematics = 0;
		Viewport->Actor->ShowFlags &= ~SHOW_Projectors;		

		// Render the shadow into a temporary texture.

		RI->PushState();

		//scion jg -- Disable z-buffer
		RI->EnableDepth(0);
		//scion jg -- Don't write to the stencil buffer
		RI->EnableStencil(0);

		// scion sz - only create color buffer for render target (less memory)
		if(!TempRenderTarget)
			TempRenderTarget = NEW FAuxRenderTarget(USize,VSize,(ETextureFormat)Format,FRenderTarget::RT_CREATE_COLOR);

		if(RI->SetRenderTarget(TempRenderTarget))
		{
			// Render the actor into the shadow texture.
			// disable projectors and light calculations
			UBOOL	StoredAcceptProjectors	= ShadowActor->bAcceptsProjectors;
			INT		StoredRendMap			= Viewport->Actor->RendMap;
			Viewport->Actor->RendMap		= REN_PlainTex;
			ShadowActor->bAcceptsProjectors	= 0;

			FShadowBitmapRenderInterface	ShadowRI(RI,ShadowDarkness * ShadowAtten);
			FShadowSceneNode				SceneNode(Viewport,TempRenderTarget,ShadowActor,Viewport->Actor,Viewport->Actor->Location,Viewport->Actor->Rotation,LightFOV);

			//scion jg -- Use the special clear function
			ClearShadowBitmap(TempRenderTarget,RI,FColor(127,127,127,255));
			
			// scion sz - shrink viewport by 2 pixel in all directions so that edges are always clear of shadow
			// why 2 pixels instead of 1?  Because of D3DTEXF_LINEAR filtering on textures.
			RI->SetViewport( 2, 2, USize-4, VSize-4 );

			// render casting geometry
			SceneNode.Render(&ShadowRI);

			Viewport->Actor->RendMap		= StoredRendMap;
			ShadowActor->bAcceptsProjectors	= StoredAcceptProjectors;

			//scion jg -- Pop the viewport change
			RI->ResetViewport();

			RI->PopState();

			// Blur the shadow into the shadow texture.

			RI->PushState();

			// scion sz - only create color buffer for render target (less memory)
			if(!ShadowTexture)
				ShadowTexture = NEW FAuxRenderTarget(USize,VSize,(ETextureFormat)Format,FRenderTarget::RT_CREATE_COLOR);

			if(RI->SetRenderTarget(ShadowTexture))
			{

// scion sz - do shadow texture blur using a 4 tap blur pixel shader
// UC2 port (M7b): this was "#if defined(_XBOX) && defined(NEW_SHADOW_BLEND)" ... "#else" ...
// "#endif". The Xbox branch runs for a device with the XboxShadows capability (UnRenDev.h;
// it draws through PS_ShadowBlur), PC's nine canvas tiles otherwise. Both bodies unchanged.
#if defined(NEW_SHADOW_BLEND)
				if( RenDevHasXboxShadows() )
				{
				//scion jg -- Use the special clear function
				ClearShadowBitmap(ShadowTexture,RI,FColor(255,255,255,255));

				DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset0, {} );
				DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset1, {} );
				DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset2, {} );
				DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset3, {} );
				DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, ShadowBitmapMaterial, {} );
				// scion sz - trying a 1221 horizontal filter
				static FColorFloat Filter[] =
				{
					1.f/6.f,1.f/6.f,1.f/6.f,1.f/6.f,
					2.f/6.f,2.f/6.f,2.f/6.f,2.f/6.f,
					2.f/6.f,2.f/6.f,2.f/6.f,2.f/6.f,
					1.f/6.f,1.f/6.f,1.f/6.f,1.f/6.f
				};
				DECLARE_STATIC_UOBJECT(	UPSBlurMaterial, PixelShaderBlurMaterial, 
				{
					PixelShaderBlurMaterial->BlurSourceMaterial = ShadowBitmapMaterial;
					PixelShaderBlurMaterial->BlurSourceOffset0 = Cast<UTexModifier>(TexOffset0);
					PixelShaderBlurMaterial->BlurSourceOffset1 = Cast<UTexModifier>(TexOffset1);
					PixelShaderBlurMaterial->BlurSourceOffset2 = Cast<UTexModifier>(TexOffset2);
					PixelShaderBlurMaterial->BlurSourceOffset3 = Cast<UTexModifier>(TexOffset3);
					//scion jg -- Use the darken blur
					PixelShaderBlurMaterial->BlurBlendMode = BLUR_SHADOW;					
					for( INT i=0; i < 4; i++ )
					{
						PixelShaderBlurMaterial->Weights[i] = Filter[i];
					}					
				} );

				static FLOAT U1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				static FLOAT V1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				static FLOAT U2[4] = {1.0f,	1.0f, 1.0f,	1.0f};
				static FLOAT V2[4] = {1.0f,	1.0f, 1.0f,	1.0f};

				FLOAT offsetX=1.0f/USize;
				//FLOAT offsetY=1.0f/VSize;

				TexOffset0->Matrix.M[2][0] = offsetX;
				TexOffset1->Matrix.M[2][0] = -offsetX/3.f;
				TexOffset2->Matrix.M[2][0] = offsetX/3.f;
				TexOffset3->Matrix.M[2][0] = -offsetX;

				ShadowBitmapMaterial->SetTextureInterface(TempRenderTarget);				

				DECLARE_STATIC_UOBJECT(
				UFinalBlend,
					BlurBlend,
				{
					BlurBlend->FrameBufferBlending = FB_Overwrite;
					BlurBlend->Material = PixelShaderBlurMaterial;
					BlurBlend->ZWrite = 0;
					BlurBlend->ZTest = 0;
					//scion jg -- Alpha blending on an L8 render target causes a GHE
					BlurBlend->OverrideAlpha = 1;
					BlurBlend->AlphaEnable = 0;
				}
				);

				RI->EnableLighting(0,1);

				FScreenPrimUtil ShadowCanvas(ShadowTexture,RI);
				ShadowCanvas.DrawTile(
					0.f, 0.f, USize, VSize,
					U1, V1,	U2, V2,
					0.0f,
					BlurBlend,
					FColor(255,255,255,255)
					);
				ShadowCanvas.Flush();
#ifdef _XBOX
				RI->InsertFence();
#endif
				}
				else
#endif // defined(NEW_SHADOW_BLEND)
				{
				//scion jg -- Clear color only, not the depth/stencil
				RI->Clear( 1, FColor(0,0,0),0,0.f,0,0xFF );

				FLOAT	Filter[3][3] = 
				{
					{ 1, 1, 1 },
					{ 1, 1, 1 },
					{ 1, 1, 1 }
				};
				FLOAT	FilterSum = 0.0f;

				for(INT Y = 0;Y < 3;Y++)
					for(INT X = 0;X < 3;X++)
						FilterSum += Filter[X][Y];

				DECLARE_STATIC_UOBJECT(
					UProxyBitmapMaterial,
					ShadowBitmapMaterial,
					{
					}
					);

				ShadowBitmapMaterial->SetTextureInterface(TempRenderTarget);

				DECLARE_STATIC_UOBJECT(	UTexMatrix,	ShadowBitmapMaterialTexMatrix, {} );
				ShadowBitmapMaterialTexMatrix->Material = ShadowBitmapMaterial;				

				DECLARE_STATIC_UOBJECT(
					UFinalBlend,
					BlurBlend,
					{
						BlurBlend->FrameBufferBlending = FB_ShadowBlend;
						BlurBlend->Material = ShadowBitmapMaterialTexMatrix;
						BlurBlend->ZWrite = 0;
						BlurBlend->ZTest = 0;
					}
					);

				RI->EnableLighting(0,1);

				//scion jg -- Only create the various internal matrices once
				// rather than nine times. This will also batch the vertices
				// making for one commit (and push buffer write) instead of nine.
				FCanvasUtil ShadowCanvas(ShadowTexture,RI);
				for(INT Y = 0;Y < 3;Y++)
				{
					for(INT X = 0;X < 3;X++)
					{
						ShadowCanvas.DrawTile(
							0,
							0,
							USize,
							VSize,
							-X * 1.5f + 1 * 1.5f,
							-Y * 1.5f + 1 * 1.5f,
							-X * 1.5f + USize + 1 * 1.5f,
							-Y * 1.5f + VSize + 1 * 1.5f,
							1.0f,
							BlurBlend,
							FColor(FPlane(1,1,1,Filter[X][Y] / FilterSum))
							);
					}
				}

				ShadowCanvas.Flush();
				RI->SetMaterial(NULL);
				}

				Invalid = 0;
				//scion jg -- Pop the render target change
				RI->ResetRenderTarget();
			}
			else
				Invalid = 1;

			//scion jg -- Pop the render target change
			RI->ResetRenderTarget();
		}
		else
			Invalid = 1;

		//scion jg -- Clean out our stencil change
		RI->EnableStencil(1);
		//scion jg -- Re-enable z-buffer
		RI->EnableDepth(1);

		RI->PopState();

		Viewport->Actor->Location = SavedLocation;
		Viewport->Actor->Rotation = SavedRotation;
		Viewport->SizeX = SavedSizeX;
		Viewport->SizeY = SavedSizeY;
		Viewport->ScaleX = SavedScaleX;
		Viewport->ScaleY = SavedScaleY;
		Viewport->ScaleFOVX = SavedScaleFOVX;
		Viewport->ScaleFOVY = SavedScaleFOVY;
		Viewport->bRenderCinematics = SavedCinematics;
		Viewport->Actor->ShowFlags = SavedShowFlags;

		Dirty = 0;
    	ShadowActor->LastRenderTime = SavedLastRenderTime; 
		END_D3D_EVENT;
	}

	if( Invalid )
		return BlobShadow;
	else
		return this;

	unguard;
}

