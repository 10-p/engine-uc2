/*=============================================================================
	D3DRenderState.cpp: Unreal Direct3D deferred state implementation.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#include "D3DDrv.h"

#ifndef FINAL_RELEASE
#define COMMIT_RENDERSTATE( a )																		\
	if( WantedState.RenderState[a] != HardwareState.RenderState[a] )								\
	{																								\
		HardwareState.RenderState[a] = WantedState.RenderState[a];									\
		RenDev->Direct3DDevice8->SetRenderState( D3D##a, WantedState.RenderState[a] );				\
		StateChanges++;																				\
	}
#define SET_RENDERSTATE( a, b )																		\
	{																								\
		RenDev->Direct3DDevice8->SetRenderState( D3D##a, b	);										\
		HardwareState.RenderState[a] = b;															\
	}


#define COMMIT_STAGESTATE( s, a )																	\
	if( WantedState.StageState[s][a] != HardwareState.StageState[s][a] )							\
	{																								\
		HardwareState.StageState[s][a] = WantedState.StageState[s][a]; 								\
		RenDev->Direct3DDevice8->SetTextureStageState( s, D3D##a, WantedState.StageState[s][a] );	\
		StateChanges++;																				\
	}
#define SET_STAGESTATE( s, a, b )																	\
	{																								\
		RenDev->Direct3DDevice8->SetTextureStageState( s, D3D##a, b );								\
		HardwareState.StageState[s][a] = b;															\
	}

// scion sz - removed appMemcpy from COMMIT/SET transform
#define COMMIT_TRANSFORM( t )																		\
	if ( (WantedState.IsDirty_Matrices & (1 << t) ) )												\
	{																								\
		RenDev->Direct3DDevice8->SetTransform( D3D##t, &WantedState.Matrices[t] );					\
		TransformChanges++;																			\
	}
#define SET_TRANSFORM( t, m )																		\
	{																								\
		RenDev->Direct3DDevice8->SetTransform( D3D##t, (D3DMATRIX*) &m );							\
	}
#else
#define COMMIT_RENDERSTATE( a )																		\
	if( WantedState.RenderState[a] != HardwareState.RenderState[a] )								\
	{																								\
		HardwareState.RenderState[a] = WantedState.RenderState[a];									\
		RenDev->Direct3DDevice8->SetRenderState( D3D##a, WantedState.RenderState[a] );				\
	}
#define SET_RENDERSTATE( a, b )																		\
	{																								\
		RenDev->Direct3DDevice8->SetRenderState( D3D##a, b	);										\
		HardwareState.RenderState[a] = b;															\
	}


#define COMMIT_STAGESTATE( s, a )																	\
	if( WantedState.StageState[s][a] != HardwareState.StageState[s][a] )							\
	{																								\
		HardwareState.StageState[s][a] = WantedState.StageState[s][a]; 								\
		RenDev->Direct3DDevice8->SetTextureStageState( s, D3D##a, WantedState.StageState[s][a] );	\
	}
#define SET_STAGESTATE( s, a, b )																	\
	{																								\
		RenDev->Direct3DDevice8->SetTextureStageState( s, D3D##a, b );								\
		HardwareState.StageState[s][a] = b;															\
	}

// scion sz - removed appMemcpy from COMMIT/SET transform
#define COMMIT_TRANSFORM( t )																		\
	if ( (WantedState.IsDirty_Matrices & (1 << t) ) )												\
	{																								\
		RenDev->Direct3DDevice8->SetTransform( D3D##t, &WantedState.Matrices[t] );					\
	}
#define SET_TRANSFORM( t, m )																		\
	{																								\
		RenDev->Direct3DDevice8->SetTransform( D3D##t, (D3DMATRIX*) &m );							\
	}
#endif

void FD3DDeferredState::Init( UD3DRenderDevice*	InRenDev )
{
	guard(FD3DDeferredState::Init);
	RenDev = InRenDev;
	
	FLOAT	Dummy0 = 0.f,
			Dummy1 = 1.0f;

	SET_RENDERSTATE( RS_FILLMODE			, D3DFILL_SOLID		);
	SET_RENDERSTATE( RS_ZWRITEENABLE		, TRUE				);
	SET_RENDERSTATE( RS_COLORWRITEENABLE	, D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_ALPHA	);
	SET_RENDERSTATE( RS_ZENABLE				, TRUE				);
	SET_RENDERSTATE( RS_ALPHATESTENABLE		, FALSE				);
	SET_RENDERSTATE( RS_BLENDOP				, D3DBLENDOP_ADD	);
	SET_RENDERSTATE( RS_SRCBLEND			, D3DBLEND_ONE		);
	SET_RENDERSTATE( RS_DESTBLEND			, D3DBLEND_ZERO		);
	SET_RENDERSTATE( RS_CULLMODE			, D3DCULL_NONE		);
	SET_RENDERSTATE( RS_ZFUNC				, D3DCMP_LESSEQUAL	);
	SET_RENDERSTATE( RS_ALPHAREF			, 0					);
	SET_RENDERSTATE( RS_ALPHAFUNC			, D3DCMP_GREATER	);
	SET_RENDERSTATE( RS_ALPHABLENDENABLE	, FALSE				);
	SET_RENDERSTATE( RS_FOGENABLE			, FALSE				);
	SET_RENDERSTATE( RS_FOGCOLOR			, 0					);
	SET_RENDERSTATE( RS_FOGSTART			, *((DWORD*)&Dummy0));
	SET_RENDERSTATE( RS_FOGEND				, *((DWORD*)&Dummy0));
	SET_RENDERSTATE( RS_ZBIAS				, 0					);
	SET_RENDERSTATE( RS_STENCILENABLE		, (RenDev->UseStencil || GIsEditor) ? TRUE : FALSE	);
	SET_RENDERSTATE( RS_STENCILFAIL			, D3DSTENCILOP_KEEP );
	SET_RENDERSTATE( RS_STENCILZFAIL		, D3DSTENCILOP_KEEP );
	SET_RENDERSTATE( RS_STENCILPASS			, D3DSTENCILOP_KEEP );
	SET_RENDERSTATE( RS_STENCILFUNC			, D3DCMP_ALWAYS		);
	SET_RENDERSTATE( RS_STENCILREF			, 0					);
	SET_RENDERSTATE( RS_STENCILMASK			, 0xFF				);
	SET_RENDERSTATE( RS_STENCILWRITEMASK	, 0xFF				);
	SET_RENDERSTATE( RS_TEXTUREFACTOR		, 0					);
	SET_RENDERSTATE( RS_LIGHTING			, 0					);
	SET_RENDERSTATE( RS_AMBIENT				, 0					);
	SET_RENDERSTATE( RS_COLORVERTEX			, TRUE				);
	SET_RENDERSTATE( RS_PATCHSEGMENTS		, *((DWORD*)&Dummy1));
	SET_RENDERSTATE( RS_SPECULARENABLE		, FALSE );


	SET_TRANSFORM( TS_VIEW					, FMatrix::Identity );
    SET_TRANSFORM( TS_PROJECTION			, FMatrix::Identity );
    SET_TRANSFORM( TS_TEXTURE0				, FMatrix::Identity );
    SET_TRANSFORM( TS_TEXTURE1				, FMatrix::Identity );
    SET_TRANSFORM( TS_TEXTURE2				, FMatrix::Identity );
    SET_TRANSFORM( TS_TEXTURE3				, FMatrix::Identity );
#ifndef _XBOX
    SET_TRANSFORM( TS_TEXTURE4				, FMatrix::Identity );
    SET_TRANSFORM( TS_TEXTURE5				, FMatrix::Identity );
    SET_TRANSFORM( TS_TEXTURE6				, FMatrix::Identity );
    SET_TRANSFORM( TS_TEXTURE7				, FMatrix::Identity );
    SET_TRANSFORM( TS_WORLD					, FMatrix::Identity );
#endif

	for( DWORD StageIndex=0; StageIndex<RenDev->DeviceCaps8.MaxTextureBlendStages; StageIndex++ )
	{
		SET_STAGESTATE( StageIndex, TSS_COLOROP					, D3DTOP_DISABLE					);
		SET_STAGESTATE( StageIndex, TSS_COLORARG1				, D3DTA_TEXTURE						);
		SET_STAGESTATE( StageIndex, TSS_COLORARG2				, D3DTA_CURRENT						);
		SET_STAGESTATE( StageIndex, TSS_ALPHAOP					, D3DTOP_DISABLE					);
		SET_STAGESTATE( StageIndex, TSS_ALPHAARG1				, D3DTA_DIFFUSE						);
		SET_STAGESTATE( StageIndex, TSS_ALPHAARG2				, D3DTA_CURRENT						);
		SET_STAGESTATE( StageIndex, TSS_TEXCOORDINDEX			, D3DTSS_TCI_PASSTHRU				);
		SET_STAGESTATE( StageIndex, TSS_ADDRESSU				, RenDev->CubemapTextureAddressing	);
		SET_STAGESTATE( StageIndex, TSS_ADDRESSV				, RenDev->CubemapTextureAddressing	);
		SET_STAGESTATE( StageIndex, TSS_TEXTURETRANSFORMFLAGS	, D3DTTFF_DISABLE					);
		SET_STAGESTATE( StageIndex, TSS_ADDRESSW				, RenDev->CubemapTextureAddressing	);
        SET_STAGESTATE( StageIndex, TSS_MIPMAPLODBIAS		    , RenDev->DefaultTexMipBias			);
		SET_STAGESTATE( StageIndex, TSS_COLORARG0				, D3DTA_CURRENT						);
		SET_STAGESTATE( StageIndex, TSS_ALPHAARG0				, D3DTA_CURRENT						);
		SET_STAGESTATE( StageIndex, TSS_RESULTARG				, D3DTA_CURRENT						);		
		SET_STAGESTATE( StageIndex, TSS_BUMPENVMAT00			, 0									);
		SET_STAGESTATE( StageIndex, TSS_BUMPENVMAT01			, 0									);
		SET_STAGESTATE( StageIndex, TSS_BUMPENVMAT10			, 0									);
		SET_STAGESTATE( StageIndex, TSS_BUMPENVMAT11			, 0									);
		SET_STAGESTATE( StageIndex, TSS_BUMPENVLSCALE			, 0									);
		SET_STAGESTATE( StageIndex, TSS_BUMPENVLOFFSET			, 0									);
#ifdef _XBOX
		SET_STAGESTATE( StageIndex, TSS_COLORSIGN				, 0									);
#endif
		SET_STAGESTATE( StageIndex, TSS_MINFILTER				, RenDev->UsedLevelOfAnisotropy	> 1 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR );
		SET_STAGESTATE( StageIndex, TSS_MAGFILTER				, RenDev->UsedLevelOfAnisotropy > 1 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR );
		SET_STAGESTATE( StageIndex, TSS_MIPFILTER				, RenDev->UseMipmapping ? (RenDev->UseTrilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT) : D3DTEXF_NONE );

		RenDev->Direct3DDevice8->SetTexture( StageIndex, NULL );
		HardwareState.Textures[StageIndex] = NULL;
	}

	HardwareState.VertexShaderEnum = WantedState.VertexShaderEnum = 0;
	HardwareState.VertexShader = NULL;
#ifdef _XBOX
	HardwareState.PixelShader = NULL;
#else
	HardwareState.PixelShaderHandle = 0;
#endif
	// scion sz - reset pixel shader
#ifdef _XBOX
	RenDev->Direct3DDevice8->SetPixelShaderProgram(HardwareState.PixelShader);
#else
	RenDev->Direct3DDevice8->SetPixelShader( HardwareState.PixelShaderHandle );
#endif
	RenDev->Direct3DDevice8->SetVertexShader( NULL );
//	RenDev->Direct3DDevice8->SetVertexShader( D3DFVF_XYZ );	

	for( INT StreamIndex = 0; StreamIndex < Min<INT>(16,RenDev->DeviceCaps8.MaxStreams); StreamIndex++ )
	{
		RenDev->Direct3DDevice8->SetStreamSource( 
			StreamIndex, 
			NULL,
			0
		);
		HardwareState.VertexStreams[StreamIndex].StreamData		= NULL;
		HardwareState.VertexStreams[StreamIndex].StreamStride	= 0;
	}

	RenDev->Direct3DDevice8->SetIndices( NULL, 0 );
	HardwareState.IndexBufferData		= NULL;
	HardwareState.IndexBufferBaseVertex	= 0;
	
	HardwareState.LightsEnabled = 0;

#ifdef VERTEXSHADERS_ALWAYS_ON
	// scion sz - make sure to always use the 192 constant mode 
	// when the fixed function pipeline is disabled
	RenDev->Direct3DDevice8->SetShaderConstantMode( D3DSCM_192CONSTANTS );
#endif
	
	appMemcpy( &WantedState, &HardwareState, sizeof(FD3DInternalState) );
	unguard;
}

void FD3DDeferredState::Commit()
{
	guard(FD3DInternalState::Commit);
	FUNC_CLOCK(StateCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_StateChangeCycles));

#ifndef FINAL_RELEASE
	// Stats.
	INT TextureChanges		= 0,
		LightSetChanges		= 0,
		LightChanges		= 0,
		StateChanges		= 0,
		TransformChanges	= 0,
		StreamSourceChanges	= 0;
#endif

#ifdef VERTEXSHADERS_ALWAYS_ON
	if( !WantedState.VertexShaderEnum )
	{
		// scion sz - alwyas use a vertex shader configuration even if one
		// was not requested
		WantedState.VertexShaderEnum = VS_Normal;
	}
#endif

	// RenderState.
	COMMIT_RENDERSTATE( RS_FILLMODE				);
	COMMIT_RENDERSTATE( RS_ZWRITEENABLE			);
	COMMIT_RENDERSTATE( RS_ZENABLE			    );
	COMMIT_RENDERSTATE( RS_COLORWRITEENABLE		);
	COMMIT_RENDERSTATE( RS_ALPHATESTENABLE		);
	COMMIT_RENDERSTATE( RS_BLENDOP				);
	COMMIT_RENDERSTATE( RS_SRCBLEND				);
	COMMIT_RENDERSTATE( RS_DESTBLEND			);
	COMMIT_RENDERSTATE( RS_CULLMODE				);
	COMMIT_RENDERSTATE( RS_ZFUNC				);
	COMMIT_RENDERSTATE( RS_ALPHAREF				);
// scion sz - removed (always D3DCMP_GREATER)
//	COMMIT_RENDERSTATE( RS_ALPHAFUNC			);
	COMMIT_RENDERSTATE( RS_ALPHABLENDENABLE		);
	COMMIT_RENDERSTATE( RS_SPECULARENABLE		);

	COMMIT_RENDERSTATE( RS_FOGENABLE			);
	// Avoid unnecessary fog state changes - especially for Kyro cards.
	if( WantedState.RenderState[ RS_FOGENABLE ] )
	{
		//scion jg -- Change the fog table mode if we are using a vertex shader
		RenDev->Direct3DDevice8->SetRenderState(D3DRS_FOGTABLEMODE,
			WantedState.VertexShaderEnum == 0 ? 3 : 0 );
		COMMIT_RENDERSTATE( RS_FOGCOLOR			);
		COMMIT_RENDERSTATE( RS_FOGSTART			);
		COMMIT_RENDERSTATE( RS_FOGEND			);
	}

	COMMIT_RENDERSTATE( RS_ZBIAS				);
	//scion jg -- Only check if something changed
	if (WantedState.bIsStencilDirty && (RenDev->UseStencil || GIsEditor))
	{
		COMMIT_RENDERSTATE( RS_STENCILENABLE		);
		COMMIT_RENDERSTATE( RS_STENCILFAIL			);
		COMMIT_RENDERSTATE( RS_STENCILZFAIL			);
		COMMIT_RENDERSTATE( RS_STENCILPASS			);
		COMMIT_RENDERSTATE( RS_STENCILFUNC			);
		COMMIT_RENDERSTATE( RS_STENCILREF			);
		COMMIT_RENDERSTATE( RS_STENCILMASK			);
		COMMIT_RENDERSTATE( RS_STENCILWRITEMASK		);
		WantedState.bIsStencilDirty = 0;
	}

	COMMIT_RENDERSTATE( RS_TEXTUREFACTOR		);

#ifndef VERTEXSHADERS_ALWAYS_ON
	COMMIT_RENDERSTATE( RS_LIGHTING				);
	COMMIT_RENDERSTATE( RS_AMBIENT				);
	COMMIT_RENDERSTATE( RS_COLORVERTEX			);
	COMMIT_RENDERSTATE( RS_PATCHSEGMENTS		);
#endif //!VERTEXSHADERS_ALWAYS_ON

	// StageState.
	for( INT StageIndex=0; StageIndex<(INT)RenDev->DeviceCaps8.MaxTextureBlendStages; StageIndex++ )
	{
		// scion sz - skip if a pixel shader is being used
#ifdef _XBOX
		if( !WantedState.PixelShader )
#else
		if( !WantedState.PixelShaderHandle )
#endif
		{
			COMMIT_STAGESTATE( StageIndex, TSS_COLOROP				);
			COMMIT_STAGESTATE( StageIndex, TSS_COLORARG0			);
			COMMIT_STAGESTATE( StageIndex, TSS_COLORARG1			);
			COMMIT_STAGESTATE( StageIndex, TSS_COLORARG2			);
			COMMIT_STAGESTATE( StageIndex, TSS_ALPHAOP				);
			COMMIT_STAGESTATE( StageIndex, TSS_ALPHAARG0			);
			COMMIT_STAGESTATE( StageIndex, TSS_ALPHAARG1			);
			COMMIT_STAGESTATE( StageIndex, TSS_ALPHAARG2			);
			COMMIT_STAGESTATE( StageIndex, TSS_RESULTARG			);
		}
		else
		{
			// this was needed to reset the bumpmapping texture stage states
			// that force the colorsign to change
			SET_STAGESTATE( StageIndex, TSS_COLOROP	, D3DTOP_DISABLE );
		}

#ifndef VERTEXSHADERS_ALWAYS_ON
		if( !WantedState.VertexShaderEnum )
		{
			// scion sz - vertex shaders handle texcoord remapping 
			COMMIT_STAGESTATE( StageIndex, TSS_TEXCOORDINDEX		);
			COMMIT_STAGESTATE( StageIndex, TSS_TEXTURETRANSFORMFLAGS);
		}
#endif //!VERTEXSHADERS_ALWAYS_ON

		COMMIT_STAGESTATE( StageIndex, TSS_ADDRESSU				);
		COMMIT_STAGESTATE( StageIndex, TSS_ADDRESSV				);		
		COMMIT_STAGESTATE( StageIndex, TSS_ADDRESSW				);
		COMMIT_STAGESTATE( StageIndex, TSS_MIPMAPLODBIAS        );		

#ifdef BUMPMAP_ENABLE
		COMMIT_STAGESTATE( StageIndex, TSS_BUMPENVMAT00			);
		COMMIT_STAGESTATE( StageIndex, TSS_BUMPENVMAT01			);
		COMMIT_STAGESTATE( StageIndex, TSS_BUMPENVMAT10			);
		COMMIT_STAGESTATE( StageIndex, TSS_BUMPENVMAT11			);
		COMMIT_STAGESTATE( StageIndex, TSS_BUMPENVLSCALE		);
		COMMIT_STAGESTATE( StageIndex, TSS_BUMPENVLOFFSET		);
#ifdef _XBOX
		COMMIT_STAGESTATE( StageIndex, TSS_COLORSIGN			);
#endif
#endif
		COMMIT_STAGESTATE( StageIndex, TSS_MINFILTER			);
		COMMIT_STAGESTATE( StageIndex, TSS_MAGFILTER			);
		COMMIT_STAGESTATE( StageIndex, TSS_MIPFILTER			);
	}

#ifndef VERTEXSHADERS_ALWAYS_ON
	if( !WantedState.VertexShaderEnum )
	{
		// Transforms.
		CLOCK(TranCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_TransformChangeCycles));
		COMMIT_TRANSFORM( TS_TEXTURE0				);
		COMMIT_TRANSFORM( TS_TEXTURE1				);
		COMMIT_TRANSFORM( TS_TEXTURE2				);
		COMMIT_TRANSFORM( TS_TEXTURE3				);
#ifndef _XBOX
		COMMIT_TRANSFORM( TS_TEXTURE4				);
		COMMIT_TRANSFORM( TS_TEXTURE5				);
		COMMIT_TRANSFORM( TS_TEXTURE6				);
		COMMIT_TRANSFORM( TS_TEXTURE7				);
#endif
		COMMIT_TRANSFORM( TS_VIEW					);
		COMMIT_TRANSFORM( TS_PROJECTION				);
		COMMIT_TRANSFORM( TS_WORLD					);
		UNCLOCK();

		// Lights.
		CLOCK(LightChanCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_LightChangeCycles));
		for( INT LightIndex = 0; LightIndex < Min<INT>(8,RenDev->DeviceCaps8.MaxActiveLights); LightIndex++ )
		{
			if( WantedState.LightsEnabled & (1 << LightIndex) )
			{
				//scion jg -- Removed the copy of the light since it was never used
				RenDev->Direct3DDevice8->SetLight( LightIndex, &WantedState.Lights[LightIndex] );
#ifndef FINAL_RELEASE
				LightChanges++;
#endif
			}

			if ((WantedState.LightsEnabled & (1 << LightIndex)) !=
				(HardwareState.LightsEnabled & (1 << LightIndex)))
			{
				RenDev->Direct3DDevice8->LightEnable( LightIndex,
					(WantedState.LightsEnabled & (1 << LightIndex)));
#ifndef FINAL_RELEASE
				LightSetChanges++;
#endif
			}
		}
		HardwareState.LightsEnabled = WantedState.LightsEnabled;
		UNCLOCK();
	}
#endif //!VERTEXSHADERS_ALWAYS_ON	

	CLOCK(TexCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_TextureChangeCycles));
	for( INT StageIndex=0; StageIndex<Min<INT>(8,RenDev->DeviceCaps8.MaxTextureBlendStages); StageIndex++ )
	{
		if( WantedState.Textures[StageIndex] != HardwareState.Textures[StageIndex] )
		{
#ifdef _XBOX //scion jg -- Attempt to do a SwitchTexture() on the xbox as it is much faster
			// Get xbox specific pointers to the textures so we can inspect them
			D3DBaseTexture* pWanted = WantedState.Textures[StageIndex];
			D3DBaseTexture* pHardware = HardwareState.Textures[StageIndex];
			// First check for two valid pointers
			UBOOL bBothAreValid = pWanted && pHardware;
			// The next two check determine the sameness rules of SwitchTexture()
			UBOOL bSameMask = 0;
			UBOOL bBothAreSwizzled = 0;
			// If true, the textures meet all of the rules of switchtexture
			UBOOL bUseSwitch = 0;
			// If they are both valid perform the other checks
			if (bBothAreValid)
			{
				// Can't switch texture on a linear so look for that. Size is
				// always zero for swizzled textures
				bBothAreSwizzled = XGIsSwizzledFormat(
					(D3DFORMAT)((pWanted->Format & D3DFORMAT_FORMAT_MASK) >>
					D3DFORMAT_FORMAT_SHIFT)) && XGIsSwizzledFormat(
					(D3DFORMAT)((pHardware->Format & D3DFORMAT_FORMAT_MASK) >>
					D3DFORMAT_FORMAT_SHIFT));
				// The two textures must be the same dimensions (2D vs 3D) and
				// the same cubemapness (both not or both are)
				bSameMask = (pWanted->Format & D3DFORMAT_DIMENSION_MASK) ==
					(pHardware->Format & D3DFORMAT_DIMENSION_MASK) &&
					(pWanted->Format & D3DFORMAT_CUBEMAP) ==
					(pHardware->Format & D3DFORMAT_CUBEMAP);
				// Check to see if we can use the switch texture call or not
				bUseSwitch = bBothAreSwizzled && bSameMask;
			}
			if (bUseSwitch)
			{
				// Use switch texture if the rules allow it since it is faster
				RenDev->Direct3DDevice8->SwitchTexture(StageIndex,
					WantedState.Textures[StageIndex]);
				//scion jg -- Attempt to catch nasty GHE errors better
				SINGLESTEPPUSHER;
			}
			else
			{
				// Otherwise do it the slower, traditional way
#endif
			RenDev->Direct3DDevice8->SetTexture( StageIndex, WantedState.Textures[StageIndex]);
			//scion jg -- Attempt to catch nasty GHE errors better
			SINGLESTEPPUSHER;
#ifdef _XBOX
			}
#endif
			//scion jg -- Moved the assignment here so that I could compare the
			// different values of the hardware & wanted
			HardwareState.Textures[StageIndex] = WantedState.Textures[StageIndex];
#ifndef FINAL_RELEASE
			TextureChanges++;
#endif
		}
	}
	UNCLOCK();

	
	// VertexShaders
	checkSlow(WantedState.VertexShader);

#ifdef _XBOX
	// scion sz - setup a vertex shader based on the current configuration
	//
#ifndef VERTEXSHADERS_ALWAYS_ON
	if( WantedState.VertexShaderEnum )
#endif
	{
		// Ambient color - always set 
		static FLOAT flAmbientColor[4] = {0.5f, 0.5f, 0.5f, 0.0f};
		FColor AmbientColor( WantedState.RenderState[RS_AMBIENT] );
		flAmbientColor[0] = AmbientColor.R * ONE_OVER_255;
		flAmbientColor[1] = AmbientColor.G * ONE_OVER_255;
		flAmbientColor[2] = AmbientColor.B * ONE_OVER_255;
		flAmbientColor[3] = (WantedState.RenderState[RS_COLORVERTEX]) ? 0.f : 1.f;
		if( WantedState.VertexShaderEnum & VS_StaticMeshVertexColor ){
			flAmbientColor[3]=1.f;
		}
		RenDev->SetVertexShaderConstant( VSC_AMBIENT_COLOR, flAmbientColor, 1 );

		// lights are grouped by type in order to
		// reduce the number of vertex shader combinations
		//scion sz/jg -- Don't turn lighting on for distortion
		if( WantedState.RenderState[RS_LIGHTING] &&
			VS_GETUNIQUESHADER(WantedState.VertexShaderEnum) != VS_Distortion &&
			VS_GETUNIQUESHADER(WantedState.VertexShaderEnum) != VS_DistortionAccum &&
			VS_GETUNIQUESHADER(WantedState.VertexShaderEnum) != VS_Projector_Atten &&
			VS_GETUNIQUESHADER(WantedState.VertexShaderEnum) != VS_Projector_Atten_Clip &&
			VS_GETUNIQUESHADER(WantedState.VertexShaderEnum) != VS_ColorRemap )
		{
			HandleVertexShaderLights();
		}

		// use static vertex colors
		// note that RS_COLORVERTEX and RS_LIGHTING are NOT mutually exclusive (you can have both on at the same time)
		// see D3DRS_DIFFUSEMATERIALSOURCE,D3DRS_SPECULARMATERIALSOURCE,D3DRS_EMISSIVEMATERIALSOURCE to see how 
		// pre-baked vertex lighting is combined with dynamic lighting
		if( WantedState.RenderState[RS_COLORVERTEX] )
		{
			WantedState.VertexShaderEnum |= VS_VertexColored;
		}

		// Set Clip Matrix - 
		static D3DXMATRIX matWVP, matWVP_T;
		static D3DXMATRIX matWV, matWV_T, matInTWV;
		if( WantedState.IsDirty_Matrices_VS & ((1<<TS_WORLD) | (1<<TS_VIEW)) )
		{
			D3DXMatrixMultiply( &matWV, (D3DXMATRIX*)&WantedState.Matrices[TS_WORLD], (D3DXMATRIX*)&WantedState.Matrices[TS_VIEW] );
			D3DXMatrixMultiply( &matWVP, &matWV, (D3DXMATRIX*)&WantedState.Matrices[TS_PROJECTION] );
			D3DXMatrixInverse( &matInTWV, NULL, &matWV );
			D3DXMatrixTranspose( &matWV_T, &matWV );
			D3DXMatrixTranspose( &matWVP_T, &matWVP );			
		}
		else if( WantedState.IsDirty_Matrices_VS & (1<<TS_PROJECTION) )
		{
			D3DXMatrixMultiply( &matWVP, &matWV, (D3DXMATRIX*)&WantedState.Matrices[TS_PROJECTION] ); 
			D3DXMatrixTranspose( &matWVP_T, &matWVP );
		}		
		RenDev->SetVertexShaderConstant( VSC_CLIP_MATRIX, (D3DXMATRIX*)&matWVP_T, 4 );

#ifndef VERTEXSHADERS_ALWAYS_ON
		// make sure world/view/proj matrices are also set in d3d since they may have been dirty before
		// using a vertex shader and the dirty flag is cleared after commit so the changes
		// would never be propogated to the next commit call
		COMMIT_TRANSFORM( TS_WORLD					);
		COMMIT_TRANSFORM( TS_VIEW					);
		COMMIT_TRANSFORM( TS_PROJECTION				);
#endif //!VERTEXSHADERS_ALWAYS_ON

		UBOOL MViewMatrixSet=0;
		// get the vertex shader enum for the current tex stage states and add it to the current VS enum
		WantedState.VertexShaderEnum |= GetTexStageVertexShader();
		for( DWORD StageIndex=0; StageIndex < RenDev->DeviceCaps8.MaxTextureBlendStages; StageIndex++ )
		{
			// if any of the camera space states were used in the tex stages then set the world to
			// camera matrix (matWV_T)
			if( WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACEPOSITION  || 
				WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACENORMAL || 
				WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR )
			{
				if( WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACENORMAL || 
					WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR ) 
				{
					RenDev->SetVertexShaderConstant( VSC_INVT_MVIEW_MATRIX, (D3DXMATRIX*)&matInTWV, 4 );
				}
				RenDev->SetVertexShaderConstant( VSC_MVIEW_MATRIX, (D3DXMATRIX*)&matWV_T, 4 );
				MViewMatrixSet=1;
				break;
			}			
		}	

		// texture transform matrices
		if( WantedState.IsDirty_Matrices_VS & (1 << TS_TEXTURE0) )
		{
			D3DXMatrixTranspose( (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE0], (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE0] );
			RenDev->SetVertexShaderConstant( VSC_TEX0_MATRIX, (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE0], 3 );
		}

		if( WantedState.IsDirty_Matrices_VS & (1 << TS_TEXTURE1) )
		{
			D3DXMatrixTranspose( (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE1], (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE1] );
			RenDev->SetVertexShaderConstant( VSC_TEX1_MATRIX, (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE1], 3 );
		}

		if( WantedState.IsDirty_Matrices_VS & (1 << TS_TEXTURE2) )
		{
			D3DXMatrixTranspose( (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE2], (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE2] );
			RenDev->SetVertexShaderConstant( VSC_TEX2_MATRIX, (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE2], 3 );
		}

		if( WantedState.IsDirty_Matrices_VS & (1 << TS_TEXTURE3) )
		{
			D3DXMatrixTranspose( (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE3], (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE3] );
			RenDev->SetVertexShaderConstant( VSC_TEX3_MATRIX, (D3DXMATRIX*)&WantedState.Matrices[TS_TEXTURE3], 3 );
		}

		if( WantedState.RenderState[ RS_FOGENABLE ] )
		{
			// Add the Fog shader to the list
			WantedState.VertexShaderEnum |= VS_Fog;
			FLOAT flStart = 1.f / (*(FLOAT*)&WantedState.RenderState[ RS_FOGEND ] -
				*(FLOAT*)&WantedState.RenderState[ RS_FOGSTART ]);
			FLOAT flFog[] =
			{
				flStart,
				*(FLOAT*)&WantedState.RenderState[ RS_FOGEND ] * flStart,
				0.f,0.f
			};
			// Now set the vertex shader constant with our fog range
			RenDev->SetVertexShaderConstant(VSC_FOG, flFog, 1);
			// scion sz -
			// ModelView matrix needed for fog calcs
			if( !MViewMatrixSet ) {
				RenDev->SetVertexShaderConstant( VSC_MVIEW_MATRIX, (D3DXMATRIX*)&matWV_T, 4 );
			}			
		}

		// set local to camera space matrix iot calculate vertex distance for distortion
		if( !MViewMatrixSet &&
			VS_GETUNIQUESHADER(WantedState.VertexShaderEnum) == VS_DistortionAccum )
		{
			RenDev->SetVertexShaderConstant( VSC_MVIEW_MATRIX, (D3DXMATRIX*)&matWV_T, 4 );
		}

		// clear dirty bits for vertex shader matrices
		WantedState.IsDirty_Matrices_VS = 0;

		// get the microcode combination required for this vertex shader enumeration
		if( HardwareState.VertexShaderEnum != WantedState.VertexShaderEnum )
		{
			FD3DCombinedVertexShader* pCombinedVertexShader = RenDev->GetCombinedVertexShader( WantedState.VertexShaderEnum );
			RenDev->Direct3DDevice8->LoadVertexShaderProgram( pCombinedVertexShader->CombinedMicrocode, 0 );			
			HardwareState.VertexShaderEnum = WantedState.VertexShaderEnum;
			RenDev->Direct3DDevice8->SelectVertexShader( WantedState.VertexShader->HandleVS, 0 );
			HardwareState.VertexShader = WantedState.VertexShader;

			// useful constants - always set
			// see ShaderConstants.vsh
			// { VSC_ZERO, VSC_ONE, VSC_NEG_TWO, VSC_ONEHALF,  VSC_255_HALF, VSC_255, VSC_ONE_OVER_255,? }	
			static FLOAT VSConstants0[8] = 
			{ 0.f, 1.f, -2.f, 0.5f,  255.5f, 255.0f, ONE_OVER_255, 0.f };
			RenDev->SetVertexShaderConstant( VSC_CONSTANTS0, VSConstants0, 2 );
		}

		// set the vertex stream declarations
		if( HardwareState.VertexShader != WantedState.VertexShader )
		{
			RenDev->Direct3DDevice8->SelectVertexShader( WantedState.VertexShader->HandleVS, 0 );
			HardwareState.VertexShader = WantedState.VertexShader;
		}

#ifdef _DEBUG
		// DEBUGSZ
		RenDev->Direct3DDevice8->SetDebugMarker( WantedState.VertexShaderEnum % MAXINT );
#endif

	}
#ifndef VERTEXSHADERS_ALWAYS_ON
	else
	if( HardwareState.VertexShaderEnum != WantedState.VertexShaderEnum || 
		WantedState.VertexShader != HardwareState.VertexShader )
	{
		// Restore from 192 constant mode to 96 for fixed function
		static DWORD ShaderConstantMode=0;
		RenDev->Direct3DDevice8->GetShaderConstantMode( &ShaderConstantMode );
		if( !(ShaderConstantMode & D3DSCM_96CONSTANTS) )
		{
			RenDev->Direct3DDevice8->SetShaderConstantMode( D3DSCM_96CONSTANTS );
		}

		HardwareState.VertexShader = WantedState.VertexShader;
		HardwareState.VertexShaderEnum = WantedState.VertexShaderEnum;
		RenDev->Direct3DDevice8->SetVertexShader( WantedState.VertexShader->Handle );
	}
#endif //!VERTEXSHADERS_ALWAYS_ON

#else
	if( WantedState.VertexShader != HardwareState.VertexShader )
	{
		HardwareState.VertexShader = WantedState.VertexShader;
		RenDev->Direct3DDevice8->SetVertexShader( WantedState.VertexShader->Handle );
	}
#endif //_XBOX

	// VertexStreams.
	for( INT StreamIndex = 0; StreamIndex < Min<INT>(16,RenDev->DeviceCaps8.MaxStreams); StreamIndex++ )
	{
		if( WantedState.VertexStreams[StreamIndex].StreamData	!= HardwareState.VertexStreams[StreamIndex].StreamData 
			||	WantedState.VertexStreams[StreamIndex].StreamStride != HardwareState.VertexStreams[StreamIndex].StreamStride )
		{
			HardwareState.VertexStreams[StreamIndex].StreamData		= WantedState.VertexStreams[StreamIndex].StreamData;
			HardwareState.VertexStreams[StreamIndex].StreamStride	= WantedState.VertexStreams[StreamIndex].StreamStride;
			RenDev->Direct3DDevice8->SetStreamSource( 
				StreamIndex, 
				WantedState.VertexStreams[StreamIndex].StreamData,
				WantedState.VertexStreams[StreamIndex].StreamStride
				);
			//scion jg -- Attempt to catch nasty GHE errors better
			SINGLESTEPPUSHER;
#ifndef FINAL_RELEASE
			StreamSourceChanges++;
#endif
		}
	}

	// Indices.
	if( WantedState.IndexBufferData != HardwareState.IndexBufferData 
		||	WantedState.IndexBufferBaseVertex != HardwareState.IndexBufferBaseVertex )
	{
		HardwareState.IndexBufferData = WantedState.IndexBufferData;
		HardwareState.IndexBufferBaseVertex = WantedState.IndexBufferBaseVertex;
		RenDev->Direct3DDevice8->SetIndices( 
			WantedState.IndexBufferData, 
			WantedState.IndexBufferBaseVertex 
			);
	}

	// PixelShader.
#ifdef _XBOX
	//jg -- Avoid the copying of the pixel shader microcode by using our
	// program directly
	if (WantedState.PixelShader != HardwareState.PixelShader)
	{
		HardwareState.PixelShader = WantedState.PixelShader;
		RenDev->Direct3DDevice8->SetPixelShaderProgram(WantedState.PixelShader);
	}
	if (WantedState.PixelShader && 
		WantedState.NumPixelShaderConstantsUsed > 0)
	{
		HRESULT hr = RenDev->Direct3DDevice8->SetPixelShaderConstant(0,
			WantedState.PixelShaderConstants, WantedState.NumPixelShaderConstantsUsed);
		check(SUCCEEDED(hr) && "SetPixelShaderConstantFailed");
		WantedState.NumPixelShaderConstantsUsed = 0;
	}
#else
	if( WantedState.PixelShaderHandle != HardwareState.PixelShaderHandle )
	{
		HardwareState.PixelShaderHandle = WantedState.PixelShaderHandle;
		RenDev->Direct3DDevice8->SetPixelShader( WantedState.PixelShaderHandle );
	}
	if( WantedState.PixelShaderHandle && 
		WantedState.NumPixelShaderConstantsUsed > 0 )
	{
		if( FAILED(RenDev->Direct3DDevice8->SetPixelShaderConstant(0, WantedState.PixelShaderConstants, WantedState.NumPixelShaderConstantsUsed)) )
		{
			appErrorf( NAME_Error, TEXT("SetPixelShaderConstant Failed"));
		}
		WantedState.NumPixelShaderConstantsUsed=0;
	}
#endif

	WantedState.IsDirty_Matrices = 0;
	WantedState.VertexShaderEnum = 0;

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_TextureChanges		) += TextureChanges;
	GStats.DWORDStats(RenDev->D3DStats.STATS_LightSetChanges	) += LightSetChanges;
	GStats.DWORDStats(RenDev->D3DStats.STATS_LightChanges		) += LightChanges;
	GStats.DWORDStats(RenDev->D3DStats.STATS_StateChanges		) += StateChanges;
	GStats.DWORDStats(RenDev->D3DStats.STATS_TransformChanges	) += TransformChanges;
	GStats.DWORDStats(RenDev->D3DStats.STATS_StreamSourceChanges) += StreamSourceChanges;
#endif
	unguard;
}

/* scion ======================================================================
* FD3DDeferredState::GetTexStageVertexShader
* Author: sz
* 
* Generate a vertex shader enumeration for the current d3d tex stage
* states.  
* ============================================================================
*/
QWORD FD3DDeferredState::GetTexStageVertexShader()
{
	guardSlow(FD3DDeferredState::CreateVertexShaderFromState);

	QWORD TexStageVertexShaderEnum = 0;

	for( DWORD StageIndex=0; StageIndex < RenDev->DeviceCaps8.MaxTextureBlendStages; StageIndex++ )
	{
		if( WantedState.Textures[StageIndex] )
		{
			// texture coordinate source mappings
			if( WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACEPOSITION  )
			{
				// camera space position used for this stage's tex coords
				VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_CAMERASPACEPOSITION, TexStageVertexShaderEnum );				
			}
			else if( WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACENORMAL )
			{
				// camera space normals used for this stage's tex coords
				VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_CAMERASPACENORMAL, TexStageVertexShaderEnum );
			}
			else if( WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX] == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR )
			{
				// camera space reflection vector used for this stage's tex coords
				VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_CAMERASPACEREFLECTIONVECTOR, TexStageVertexShaderEnum );
			}
			else
			{
				// remap this stage's tex coords
				VS_SETTEXSOURCE( StageIndex, WantedState.StageState[StageIndex][TSS_TEXCOORDINDEX], TexStageVertexShaderEnum );
			}

			// set texture transforms
			if( WantedState.StageState[StageIndex][TSS_TEXTURETRANSFORMFLAGS] > 0 )
			{	
				if( WantedState.StageState[StageIndex][TSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED )
				{
					// use a 4x3 matrix multiply with projected coordinates (w is valid)
					VS_SETTEXTRANSFORM( StageIndex, VS_Tex_Transform_m4x3_Projected, TexStageVertexShaderEnum );
				}
				else if( VS_GETTEXSOURCE( StageIndex, TexStageVertexShaderEnum ) >= VS_Tex_Source_CAMERASPACEPOSITION )
				{
					// use a 4x3 matrix multiply with non projected coordinates 
					VS_SETTEXTRANSFORM( StageIndex, VS_Tex_Transform_m4x3, TexStageVertexShaderEnum );
				}
				else
				{
					// use a 4x2 matrix multiply
					VS_SETTEXTRANSFORM( StageIndex, VS_Tex_Transform_m4x2, TexStageVertexShaderEnum );
				}

			}
			else
			{
				// do not apply a matrix multiply
				VS_SETTEXTRANSFORM( StageIndex, VS_Tex_Transform_None, TexStageVertexShaderEnum );
			}
		}
		else
		{
			// don't output a texture coordinate if the stage is not using a texture
			VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_None, TexStageVertexShaderEnum );				
		}
	}

	return TexStageVertexShaderEnum;

	unguardSlow;
}

// scion sz - 
#ifdef HARDCODED_VS_LIGHTS
void FD3DDeferredState::HandleVertexShaderLights()
{
	// set flag in enumeration to enable lighting 
	WantedState.VertexShaderEnum |= VS_Lit;

	// entries [0,4] are the light directions
	// entries [5,9] are the light colors
	static D3DXVECTOR4 LightConstants[VS_Lights_Directional_MAX*2];
	appMemzero( LightConstants, sizeof(LightConstants) );

	if( WantedState.LightsEnabled )
	{
		// needed to xform world space normals to local 
		static D3DMATRIX WorldInvTransposeMat;
		D3DXMatrixTranspose( (D3DXMATRIX*)&WorldInvTransposeMat, (D3DXMATRIX*)GetTransform(TS_WORLD) );
		
		// set all hard coded lights
		for( INT LightIndex = 0; LightIndex < VS_Lights_Directional_MAX; LightIndex++ )
		{
			if( WantedState.LightsEnabled & (1 << LightIndex) )
			{
				// hard coding to only support directional lights
				checkSlow(WantedState.Lights[LightIndex].Type == D3DLIGHT_DIRECTIONAL);
				// set the local space direction vector for this light
				D3DXVECTOR3* pLocalLightDir = (D3DXVECTOR3*)&LightConstants[LightIndex];			
				D3DXVec3TransformNormal( pLocalLightDir, (D3DXVECTOR3*)&WantedState.Lights[LightIndex].Direction, (D3DXMATRIX*)&WorldInvTransposeMat );
				D3DXVec3Normalize( pLocalLightDir, pLocalLightDir );
				// set the color for this light			
				appMemcpy( &LightConstants[LightIndex+VS_Lights_Directional_MAX], &WantedState.Lights[LightIndex].Diffuse, sizeof(D3DXVECTOR4) );
			}
		}
	}	

	// set constants
    RenDev->SetVertexShaderConstant( VSC_HARD_LIGHT_BASE, LightConstants, VS_Lights_Directional_MAX*2 );

}
#else
void FD3DDeferredState::HandleVertexShaderLights()
{
	// Lights Arrays
	static TArray<D3DLIGHT8*> PointLights;
	static TArray<D3DLIGHT8*> DirectionalLights;
	static TArray<D3DLIGHT8*> SpotLights;

	PointLights.Reset();
	DirectionalLights.Reset();
	SpotLights.Reset();

	// set flag in enumeration to enable lighting 
	WantedState.VertexShaderEnum |= VS_Lit;

	for( INT LightIndex = 0; LightIndex < VS_Lights_MAX; LightIndex++ )
	{
		if( WantedState.LightsEnabled & (1 << LightIndex) )
		{
			if( WantedState.Lights[LightIndex].Type == D3DLIGHT_DIRECTIONAL &&
				DirectionalLights.Num() < VS_Lights_Directional_MAX )
			{
				DirectionalLights.AddItem( &WantedState.Lights[LightIndex] );
			}
			else if( WantedState.Lights[LightIndex].Type == D3DLIGHT_POINT &&
				PointLights.Num() < VS_Lights_Point_MAX )
			{
				PointLights.AddItem( &WantedState.Lights[LightIndex] );						
			}
			else if( WantedState.Lights[LightIndex].Type == D3DLIGHT_SPOT &&
				PointLights.Num() < VS_Lights_Point_MAX )
			{
				// spotlights not supported yet
				// SpotLights.AddItem( (DWORD)&WantedState.Lights[LightIndex] );
				PointLights.AddItem( &WantedState.Lights[LightIndex] );
			}
		}
	}

	// set vertex shader enumeration based on enabled lights and their types
	// also set constants for each light
	INT VSLightIndex=0,i;
	for( i = 0; i < DirectionalLights.Num() && VSLightIndex < VS_Lights_MAX; i++ )
	{
		VS_SETLIGHTTYPE( VSLightIndex, VS_Light_Type_Directional, WantedState.VertexShaderEnum );
		SetVertexShaderDirectionalLight( VSLightIndex, DirectionalLights(i) );
		VSLightIndex++;
	}
	for( i = 0; i < PointLights.Num() && VSLightIndex < VS_Lights_MAX; i++ )
	{
		VS_SETLIGHTTYPE( VSLightIndex, VS_Light_Type_Point, WantedState.VertexShaderEnum );
		SetVertexShaderPointLight( VSLightIndex, PointLights(i) );
		VSLightIndex++;
	}
	for( i = 0; i < SpotLights.Num() && VSLightIndex < VS_Lights_MAX; i++ )
	{
		VS_SETLIGHTTYPE( VSLightIndex, VS_Light_Type_Spot, WantedState.VertexShaderEnum );
		SetVertexShaderSpotLight( VSLightIndex, SpotLights(i) );
		VSLightIndex++;
	}
}
#endif

// scion sz -
//
void FD3DDeferredState::SetVertexShaderPointLight( INT LightID, D3DLIGHT8* Light )
{
	guardSlow(FD3DDeferredState::SetVertexShaderPointLight);
	checkSlow( Light );
	checkSlow( LightID < VS_Lights_MAX );

	// start address for the constant registers of this light
	INT lightAddr = VSC_LIGHT_BASE + LightID*VSC_LIGHT_SIZE;

	// transform world space light position to local object space using the Inverse World matrix
	D3DXVECTOR4 LocalLightPos;
	D3DXVec3Transform( &LocalLightPos, (D3DXVECTOR3*)&Light->Position, (D3DXMATRIX*)GetTransform(TS_WORLDINV) );

	// store squared light range in position.w component
	LocalLightPos.w = (FLOAT)Light->Range * (FLOAT)Light->Range;

	// The light direction is not used for point lights so we use it to pass D3D attenuation factors
	D3DXVECTOR4 LocalLightDir;
	LocalLightDir.x = Light->Attenuation0;
	LocalLightDir.y = Light->Attenuation1 * 0.5f;
	LocalLightDir.z = Light->Attenuation2;
	LocalLightDir.w = (FLOAT)lightAddr;

	// NOTE: the current light address is stored in the w-component 
	// so that we can offset to the next light in the vertex shader if there is one.

	// set vertex shader constants for light position, attenuation factors, and color.
	// note that the constants are relative to the current lightAddr
	// so that we can handle a variety of light configurations
	RenDev->SetVertexShaderConstant( lightAddr+VSC_LIGHT_POS_OFFSET, &LocalLightPos, 1 );
	RenDev->SetVertexShaderConstant( lightAddr+VSC_LIGHT_DIR_OFFSET, &LocalLightDir, 1 );
	RenDev->SetVertexShaderConstant( lightAddr+VSC_LIGHT_DIFFUSE_OFFSET, &Light->Diffuse, 1 );

	unguardSlow;
}

// scion sz -
//
void FD3DDeferredState::SetVertexShaderDirectionalLight( INT LightID, D3DLIGHT8* Light )
{
	guardSlow(FD3DDeferredState::SetVertexShaderDirectionalLight);
	checkSlow( Light );
	checkSlow( LightID < VS_Lights_MAX );

	// start address for the constant registers of this light
	INT lightAddr = VSC_LIGHT_BASE + LightID*VSC_LIGHT_SIZE;

	// transform light's normal vector to local object space
	D3DXVECTOR4 LocalLightDir;
	D3DMATRIX WorldInvTransposeMat;
	D3DXMatrixTranspose( (D3DXMATRIX*)&WorldInvTransposeMat, (D3DXMATRIX*)GetTransform(TS_WORLD) );
	D3DXVec3TransformNormal( (D3DXVECTOR3*)&LocalLightDir, (D3DXVECTOR3*)&Light->Direction, (D3DXMATRIX*)&WorldInvTransposeMat );
	D3DXVec3Normalize( (D3DXVECTOR3*)&LocalLightDir, (D3DXVECTOR3*)&LocalLightDir );

	// NOTE: the current light address is stored in the w-component 
	// so that we can offset to the next light in the vertex shader if there is one.
	LocalLightDir.w = (FLOAT)lightAddr;

	// set vertex shader constants for light direction and color
	// note that the constants are relative to the current lightAddr
	// so that we can handle a variety of light configurations
	RenDev->SetVertexShaderConstant( lightAddr+VSC_LIGHT_DIR_OFFSET, &LocalLightDir, 1 );
	RenDev->SetVertexShaderConstant( lightAddr+VSC_LIGHT_DIFFUSE_OFFSET, &Light->Diffuse, 1 );

	unguardSlow;
}

// scion sz - 
// 
void FD3DDeferredState::SetVertexShaderSpotLight( INT LightID, D3DLIGHT8* Light )
{	
	guardSlow(FD3DDeferredState::SetVertexShaderSpotLight);
	check( Light );
	check( LightID < VS_Lights_MAX );

	// NOT IMPLEMENTED
	//

	unguardSlow;
}


void FD3DDeferredState::SetRenderState( ED3DRenderState State, DWORD Value )
{
	WantedState.RenderState[State] = Value;
}
void FD3DDeferredState::SetTextureStageState( DWORD Stage, ED3DTextureStateStage State, DWORD Value )
{
	WantedState.StageState[Stage][State]  = Value;
}
void FD3DDeferredState::SetVertexShader( FD3DVertexShader* VertexShader )
{
	WantedState.VertexShader = VertexShader;
}
#ifdef _XBOX
//jg -- Avoid the copying of the pixel shader microcode by using our
// program directly
void FD3DDeferredState::SetPixelShader(D3DPIXELSHADERDEF* PixelShader)
{
	WantedState.PixelShader = PixelShader;
}
#else
void FD3DDeferredState::SetPixelShader( DWORD Handle )
{
	WantedState.PixelShaderHandle = Handle;
}
#endif
void FD3DDeferredState::SetStreamSource( DWORD StreamNumber, IDirect3DVertexBuffer8* StreamData, DWORD StreamStride )
{
	WantedState.VertexStreams[StreamNumber].StreamData   = StreamData;
	WantedState.VertexStreams[StreamNumber].StreamStride = StreamStride;
}
void FD3DDeferredState::SetIndices( IDirect3DIndexBuffer8* IndexData, DWORD BaseVertexIndex )
{
	WantedState.IndexBufferData	= IndexData;
	WantedState.IndexBufferBaseVertex = BaseVertexIndex;
}
void FD3DDeferredState::SetTexture( DWORD Stage, IDirect3DBaseTexture8* Texture )
{
	WantedState.Textures[Stage]	= Texture;
}
void FD3DDeferredState::SetTransform( ED3DTransformState State, CONST D3DMATRIX* Matrix )
{
	appMemcpy( &WantedState.Matrices[State], Matrix, sizeof( D3DMATRIX ) );
	WantedState.IsDirty_Matrices |= (1 << State);
	WantedState.IsDirty_Matrices_VS |= (1 << State);	// scion sz - mirror IsDirtyMatrices for vertex shaders
}
void FD3DDeferredState::SetLight( DWORD Index, CONST D3DLIGHT8* Light )
{
	appMemcpy( &WantedState.Lights[Index], Light, sizeof(D3DLIGHT8) );
}
void FD3DDeferredState::LightEnable( DWORD LightIndex, BOOL bEnable )
{
	WantedState.LightsEnabled = ((WantedState.LightsEnabled & ~(1 << LightIndex)) |
		bEnable << LightIndex);
}
void FD3DDeferredState::DeleteVertexShader( DWORD Handle )
{	
	if( !Handle )
		return;

	if( HardwareState.VertexShader &&
		(HardwareState.VertexShader->Handle == Handle || HardwareState.VertexShader->HandleVS == Handle) )
	{
		HardwareState.VertexShader = NULL;
		RenDev->Direct3DDevice8->SetVertexShader( D3DFVF_XYZ );
	}
	HRESULT	Result = RenDev->Direct3DDevice8->DeleteVertexShader(Handle);
	if( FAILED(Result) )
		appErrorf(TEXT("DeleteVertexShader failed(%s)."),*D3DError(Result));
}
void FD3DDeferredState::DeletePixelShader( DWORD Handle )
{	
#ifndef _XBOX//jg -- Not used with direct shader programs
	if( HardwareState.PixelShaderHandle == Handle )
	{
		HardwareState.PixelShaderHandle = 0;
		RenDev->Direct3DDevice8->SetPixelShader( 0 );
	}
	HRESULT	Result = RenDev->Direct3DDevice8->DeletePixelShader(Handle);
	if( FAILED(Result) )
		appErrorf(TEXT("DeletePixelShader failed(%s)."),*D3DError(Result));
#else
	//jg -- Zero the program but don't delete
	RenDev->Direct3DDevice8->SetPixelShaderProgram(NULL);
#endif
}
void FD3DDeferredState::UnSetVertexBuffer( IDirect3DVertexBuffer8* StreamData )
{
	for( INT StreamIndex = 0; StreamIndex < Min<INT>(16,RenDev->DeviceCaps8.MaxStreams); StreamIndex++ )
	{
		if( HardwareState.VertexStreams[StreamIndex].StreamData == StreamData )
		{
			HardwareState.VertexStreams[StreamIndex].StreamData   = NULL;
			HardwareState.VertexStreams[StreamIndex].StreamStride = 0;
			RenDev->Direct3DDevice8->SetStreamSource( StreamIndex, NULL, 0 );
			//scion jg -- Attempt to catch nasty GHE errors better
			SINGLESTEPPUSHER;
		}
		if( WantedState.VertexStreams[StreamIndex].StreamData == StreamData )
		{
			WantedState.VertexStreams[StreamIndex].StreamData   = NULL;
			WantedState.VertexStreams[StreamIndex].StreamStride = 0;
		}
	}
}

/* scion ======================================================================
 * FD3DDeferredState::ResetLights
 * Author: jg
 * 
 * Disables all set lights
 *
 * ============================================================================
 */
void FD3DDeferredState::ResetLights(void)
{
// scion sz - not needed
#ifndef VERTEXSHADERS_ALWAYS_ON
	// Tell D3D we aren't using them
	for (INT Light = 0; Light < 8; Light++)
	{
		RenDev->Direct3DDevice8->LightEnable(Light,0);
	}
#endif
	// Now zero our bit mask
	HardwareState.LightsEnabled = WantedState.LightsEnabled = 0;
}

/* scion ======================================================================
 * FD3DDeferredState::SetRenderStateDirect
 * Author: jg
 * 
 * Sets the specified render state directly into D3D without having to go
 * through the deferred commit. Both the wanted & hardware states are kept in
 * sync.
 *
 * input:	State -- The render state to change
 *			Value -- The new value to set it to
 *
 * ============================================================================
 */
void FD3DDeferredState::SetRenderStateDirect(BYTE State,DWORD Value)
{
	WantedState.RenderState[State] = Value;
	HardwareState.RenderState[State] = Value;
	RenDev->Direct3DDevice8->SetRenderState((D3DRENDERSTATETYPE)State,Value);
}
