/*=============================================================================
	D3DRenderState.h: Unreal Direct3D deferred state header.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#ifndef HEADER_D3DRENDERSTATE
#define HEADER_D3DRENDERSTATE


enum ED3DRenderState
{
	RS_FILLMODE							= 0,
	RS_ZWRITEENABLE,
	RS_ZENABLE,
	RS_ALPHATESTENABLE,
	// scion sz - change blend operation (for distortion)
	RS_BLENDOP,		
	RS_SRCBLEND,
	RS_DESTBLEND,
	RS_CULLMODE,
	RS_ZFUNC,
	RS_ALPHAREF,
	RS_ALPHAFUNC,
	RS_ALPHABLENDENABLE,
	RS_FOGENABLE,
	RS_FOGCOLOR,
	RS_FOGSTART,
	RS_FOGEND,
	RS_ZBIAS,
	RS_STENCILENABLE,
	RS_STENCILFAIL,
	RS_STENCILZFAIL,
	RS_STENCILPASS,
	RS_STENCILFUNC,
	RS_STENCILREF,
	RS_STENCILMASK,
	RS_STENCILWRITEMASK,
	RS_TEXTUREFACTOR,
	RS_LIGHTING,
	RS_AMBIENT,
	RS_COLORVERTEX,
	RS_PATCHSEGMENTS,
	// scion sz - toggle color writes
	RS_COLORWRITEENABLE,
	// scion sz - enable specular color (oD1/v1) output
	RS_SPECULARENABLE,
	RS_MAX
};

enum ED3DTextureStateStage
{
	TSS_COLOROP							= 0,
	TSS_COLORARG1,
	TSS_COLORARG2,
	TSS_ALPHAOP,
	TSS_ALPHAARG1,
	TSS_ALPHAARG2,
	TSS_TEXCOORDINDEX,
	TSS_ADDRESSU,
	TSS_ADDRESSV,
	TSS_TEXTURETRANSFORMFLAGS,
	TSS_ADDRESSW,
	TSS_COLORARG0,
	TSS_ALPHAARG0,
	TSS_RESULTARG,
    TSS_MIPMAPLODBIAS,
	TSS_COLORSIGN,
	TSS_BUMPENVMAT00,  
    TSS_BUMPENVMAT01,  
    TSS_BUMPENVMAT11,  
    TSS_BUMPENVMAT10,  
    TSS_BUMPENVLSCALE,  
    TSS_BUMPENVLOFFSET,
	TSS_MAGFILTER,
	TSS_MINFILTER,
	TSS_MIPFILTER,
	TSS_MAX
};


enum ED3DTransformState
{
    TS_TEXTURE0				= 0,	//!! vogel: this order is required.
    TS_TEXTURE1,
    TS_TEXTURE2,
    TS_TEXTURE3,
#ifndef _XBOX						// scion sz - not needed on Xbox
    TS_TEXTURE4,
    TS_TEXTURE5,
    TS_TEXTURE6,
    TS_TEXTURE7,
#endif
    TS_VIEW,
    TS_PROJECTION,
	TS_WORLD,						//!! macro in D3D
	TS_WORLDINV,					// scion sz - Inverse of World Matrix
	TS_MAX
};

class FD3DDeferredState
{
public:
	void Init( UD3DRenderDevice* InRenDev );
	void Commit();

	void SetRenderState( ED3DRenderState State, DWORD Value );
	void SetTextureStageState( DWORD Stage, ED3DTextureStateStage State, DWORD Value );
	void SetVertexShader( FD3DVertexShader* VertexShader );
	void SetStreamSource( DWORD StreamNumber, IDirect3DVertexBuffer8* StreamData, DWORD Stride );
	void SetIndices( IDirect3DIndexBuffer8* pIndexData, DWORD BaseVertexIndex );
	void SetTexture( DWORD Stage, IDirect3DBaseTexture8* pTexture );
	void SetTransform( ED3DTransformState State, CONST D3DMATRIX* pMatrix );	
	void SetLight( DWORD Index, CONST D3DLIGHT8* pLight );
	void LightEnable( DWORD LightIndex, BOOL bEnable );
	void DeleteVertexShader( DWORD Handle );
	void UnSetVertexBuffer( IDirect3DVertexBuffer8* StreamData );
#ifdef _XBOX
	void SetPixelShader(D3DPIXELSHADERDEF* PixelShader);
#else
	void SetPixelShader( DWORD Handle );
#endif
	void DeletePixelShader( DWORD Handle );
	//scion jg -- Allows you to set a state and have it committed immediately
	// without going through FD3DDeferredState::Commmit()
	void SetRenderStateDirect(BYTE State,DWORD Value);

	// scion sz - generate the vertex shader enumeration for the current d3d texture stage states
	FORCEINLINE QWORD GetTexStageVertexShader();
	// scion sz - set lights
	FORCEINLINE void HandleVertexShaderLights();

	//scion jg -- Clear the lights that were previously set
	void ResetLights(void);
	//scion jg -- Lights are now only stored in the wanted state, so get the
	// light data from here rather than the old current state system
	FORCEINLINE D3DLIGHT8* GetLight(INT LightIndex)
	{
		return &WantedState.Lights[LightIndex];
	}

	// scion sz - used during commit to setup vertex shader constants for lights
	FORCEINLINE void SetVertexShaderPointLight( INT LightID, D3DLIGHT8* Light );
	FORCEINLINE void SetVertexShaderDirectionalLight( INT LightID, D3DLIGHT8* Light );
	FORCEINLINE void SetVertexShaderSpotLight( INT LightID, D3DLIGHT8* Light );

	// scion sz - get the current state's transform matrices
	FORCEINLINE D3DMATRIX* GetTransform( ED3DTransformState State ) { return &WantedState.Matrices[State]; }
	FORCEINLINE D3DMATRIX* GetTransform( ED3DTransformState State, D3DMATRIX* pMatrix ) 
	{
		appMemcpy( pMatrix, &WantedState.Matrices[State], sizeof( D3DMATRIX ) );
		return &WantedState.Matrices[State]; 
	}
	
	// scion sz - set a pixel shader constant
	FORCEINLINE void SetPixelShaderConstant( INT Register, const void* pConstantData, DWORD ConstantCount )
	{
		guard(FD3DDeferredState::SetPixelShaderConstant);
		checkSlow(Register>=0 && Register<8);
		checkSlow(ConstantCount>0);
		checkSlow(pConstantData);
		checkSlow(ConstantCount<8);

		appMemcpy( &WantedState.PixelShaderConstants[Register], pConstantData, ConstantCount*4*sizeof(FLOAT) );
		if( WantedState.NumPixelShaderConstantsUsed < (Register+(INT)ConstantCount) )
			WantedState.NumPixelShaderConstantsUsed = Register+(INT)ConstantCount;

		unguard;
	}

	//scion jg -- Mark the stencil states as needing updating
	FORCEINLINE void SetStencilDirty(void) { WantedState.bIsStencilDirty = 1; }

	// scion sz - get the current desired vertex shader enumeration
	FORCEINLINE QWORD	GetVertexShaderEnum() { return WantedState.VertexShaderEnum; }
	FORCEINLINE void	SetVertexShaderEnum( QWORD VertexShaderEnum ) { WantedState.VertexShaderEnum = VertexShaderEnum; }

	// scion sz - get the current stream declaration
	FORCEINLINE FShaderDeclaration*	GetShaderDeclaration() { return &WantedState.ShaderDeclaration; }
	FORCEINLINE void				SetShaderDeclaration( FShaderDeclaration& ShaderDeclaration ) { WantedState.ShaderDeclaration = ShaderDeclaration; }

protected:
	struct FD3DInternalState
	{
		DWORD						RenderState[RS_MAX];
		DWORD						StageState[MAX_STAGES][TSS_MAX];
		D3DMATRIX					Matrices[TS_MAX];
		DWORD						IsDirty_Matrices;
		DWORD						IsDirty_Matrices_VS;				// scion sz - mirror IsDirty_Matrices for vertex shaders

		//scion jg -- Use a flag to determine when to set these states
		BYTE bIsStencilDirty;

		// scion sz --
		FShaderDeclaration			ShaderDeclaration;
		QWORD						VertexShaderEnum;

		FD3DVertexShader*			VertexShader;
#ifdef _XBOX
		//jg -- Points directly to the microcode program
		D3DPIXELSHADERDEF*			PixelShader;
#else
		DWORD						PixelShaderHandle;
#endif

        struct FD3DDeferredPixelShaderConstant
		{
			FLOAT X,Y,Z,W;
		}							PixelShaderConstants[8];
		BYTE							NumPixelShaderConstantsUsed;
		
		struct FD3DDeferredVertexStream
		{
			IDirect3DVertexBuffer8*		StreamData;
			UINT						StreamStride;
		}							VertexStreams[16];

		IDirect3DIndexBuffer8*		IndexBufferData;
		UINT						IndexBufferBaseVertex;

		IDirect3DBaseTexture8*		Textures[8];
		//scion jg -- Changed from an array to a bit mask
		BYTE						LightsEnabled;
	};

	//scion jg -- Hardware state doesn't need to store lights
	// so only store them in the wanted state
	struct FD3DWantedState : public FD3DInternalState
	{
		D3DLIGHT8 Lights[8];
	};

	UD3DRenderDevice*				RenDev;
	FD3DWantedState				WantedState;
	FD3DInternalState				HardwareState;
};

#endif