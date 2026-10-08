/*=============================================================================
	UC2GLShadowCheck.cpp: numeric check of the projected-shadow textures, end to end
	(M7b; docs/porting/analysis/projectors.md). Debug only, not Epic's.

	Console: GLSHADOWCHECK (runs at the start of the next frame). For every shadow material
	(UShadowBitmapMaterial) whose two render targets exist, both are read back as the last
	frame left them: the caster target (TextureInterfaces[1], what UShadowBitmapMaterial::Get
	drew the shadow actor into) and the shadow texture (TextureInterfaces[0], what the projector
	projects). The shadow texture is then recomputed on the CPU from the caster target with the
	filter of the branch the engine took, and compared texel by texel:

	  Xbox (FRenderCaps::XboxShadows; UnShadowProjector.cpp, PS_ShadowBlur.psh):
	    S(x,y) = 1 - sum_t w_t * C(bilinear at ((x + 0.5 + o_t)/W, (y + 0.5)/H))
	    o = +1, -1/3, +1/3, -1 texels, w = 1/6, 2/6, 2/6, 1/6 (Epic's "1221 horizontal filter")
	  PC (nine FCanvasUtil tiles, FB_ShadowBlend = src x srcalpha + dst, vertex alpha 1/9 = 28/255):
	    S(x,y) = sum over (dx,dy) in {+1.5, 0, -1.5}^2 of round( C.rgb x C.a/255 x 28/255 ), clamped

	  UC2GLDrv: shadowcheck <material> <W>x<H> branch=<xbox|pc> caster values: <value:count ...>
	             shadow min=<v> neutral=<v> darker-texels=<n>; recomputed: largest error <e> of 255, mean <m>
	  UC2GLDrv: shadowcheck summary: <n> shadow textures, <n> within 2/255 ...

	The caster values show the other half of the difference: on Xbox the caster is drawn in
	ShadowDarkness grey (200) on 127, on PC it is 127 grey with alpha 255 - 200 = 55.
=============================================================================*/

#include "UC2GLDrv.h"

struct FShadowPicture
{
	INT				W, H;
	TArray<BYTE>	Data;	// RGBA, top row first (render targets are stored that way)
	FShadowPicture( INT InW, INT InH ) : W(InW), H(InH) { Data.AddZeroed( W * H * 4 ); }
	FORCEINLINE FLOAT At( INT X, INT Y, INT C ) { return Data( (Clamp(Y,0,H-1) * W + Clamp(X,0,W-1)) * 4 + C ); }
	FLOAT Bilinear( FLOAT U, FLOAT V, INT C )
	{
		FLOAT	X = U * W - 0.5f, Y = V * H - 0.5f;
		INT		X0 = appFloor(X), Y0 = appFloor(Y);
		FLOAT	FX = X - X0, FY = Y - Y0;
		return	(At(X0,Y0,C) * (1.f-FX) + At(X0+1,Y0,C) * FX) * (1.f-FY)
			+	(At(X0,Y0+1,C) * (1.f-FX) + At(X0+1,Y0+1,C) * FX) * FY;
	}
};

static UBOOL ReadTargetPicture( UUC2GLRenderDevice* RenDev, FRenderTarget* Target, FShadowPicture& Out )
{
	FOpenGLRenderInterface* RI = &RenDev->RenderInterface;
	FOpenGLTexture* Texture = RI->CacheTexture( Target );
	if( !Texture || !Texture->FramebufferID )
		return 0;
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Texture->FramebufferID );
	RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	RenDev->glReadPixels( 0, 0, Out.W, Out.H, GL_RGBA, GL_UNSIGNED_BYTE, &Out.Data(0) );
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RI->SavedRenderTargetState.GetCurrentState()->Framebuffer );
	return 1;
}

void UC2GL_RunShadowCheck( UUC2GLRenderDevice* RenDev, UViewport* Viewport )
{
	guard(UC2GL_RunShadowCheck);
	UBOOL	Xbox = RenDev->GetRenderCaps()->XboxShadows;
	INT		Checked = 0, Good = 0;
	FLOAT	WorstError = 0.f;
	for( TObjectIterator<UShadowBitmapMaterial> It; It; ++It )
	{
		UShadowBitmapMaterial* M = *It;
		FAuxRenderTarget* Shadow	= (FAuxRenderTarget*)M->TextureInterfaces[0];
		FAuxRenderTarget* Caster	= (FAuxRenderTarget*)M->TextureInterfaces[1];
		if( !Shadow || !Caster || M->Invalid || M->bBlobShadow )
			continue;
		INT W = Shadow->GetWidth(), H = Shadow->GetHeight();
		if( Caster->GetWidth() != W || Caster->GetHeight() != H )
			continue;
		FShadowPicture C( W, H ), S( W, H );
		if( !ReadTargetPicture( RenDev, Caster, C ) || !ReadTargetPicture( RenDev, Shadow, S ) )
			continue;
		Checked++;

		// The caster target's values (red and alpha): two of them on either branch.
		TMap<INT,INT> Values;
		for( INT i=0; i<W*H; i++ )
		{
			INT Key = C.Data(i*4) * 256 + C.Data(i*4+3);
			INT* Count = Values.Find( Key );
			if( Count ) (*Count)++; else Values.Set( Key, 1 );
		}
		FString ValueList;
		INT Listed = 0;
		for( TMap<INT,INT>::TIterator V(Values); V && Listed < 6; ++V, Listed++ )
			ValueList += FString::Printf( TEXT(" %i/a%i:%i"), V.Key() / 256, V.Key() & 255, V.Value() );
		if( Values.Num() > 6 )
			ValueList += FString::Printf( TEXT(" (+%i more)"), Values.Num() - 6 );

		// The shadow texture recomputed from the caster target.
		static const FLOAT XOff[4] = { 1.f, -1.f/3.f, 1.f/3.f, -1.f }, XW[4] = { 1.f/6.f, 2.f/6.f, 2.f/6.f, 1.f/6.f };
		FLOAT	MaxError = 0.f;
		DOUBLE	SumError = 0.0;
		INT		MinValue = 255, Darker = 0;
		FLOAT	Neutral = Xbox ? 255.f - 127.f : 0.f;
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
			{
				FLOAT Expected;
				if( Xbox )
				{
					FLOAT Sum = 0.f;
					for( INT t=0; t<4; t++ )
						Sum += XW[t] * C.Bilinear( (x + 0.5f + XOff[t]) / W, (y + 0.5f) / H, 0 );
					Expected = Clamp( 255.f - Sum, 0.f, 255.f );
				}
				else
				{
					FLOAT Sum = 0.f;
					for( INT dy=-1; dy<=1; dy++ )
						for( INT dx=-1; dx<=1; dx++ )
						{
							FLOAT U = (x + 0.5f + 1.5f * dx) / W, V = (y + 0.5f + 1.5f * dy) / H;
							Sum += appRound( C.Bilinear(U,V,0) * (C.Bilinear(U,V,3) / 255.f) * (28.f / 255.f) );
						}
					Expected = Min( Sum, 255.f );
				}
				FLOAT Got = S.At(x,y,0);
				FLOAT Error = Abs( Got - Expected );
				MaxError = Max( MaxError, Error );
				SumError += Error;
				MinValue = Min<INT>( MinValue, (INT)Got );
				if( x == 0 && y == 0 && !Xbox )
					Neutral = Got;
			}
		for( INT i=0; i<W*H; i++ )
			if( S.Data(i*4) + 8 < Neutral )
				Darker++;
		if( MaxError <= 2.f )
			Good++;
		WorstError = Max( WorstError, MaxError );
		debugf( NAME_Init, TEXT("UC2GLDrv: shadowcheck %s of %s %ix%i branch=%s caster values (red/alpha:texels):%s; shadow min=%i neutral=%.0f darker-texels=%i; recomputed from the caster: largest error %.2f of 255, mean %.3f"),
			M->GetName(), M->ShadowActor ? M->ShadowActor->GetName() : TEXT("None"), W, H, Xbox ? TEXT("xbox") : TEXT("pc"), *ValueList,
			MinValue, Neutral, Darker, MaxError, (FLOAT)(SumError / (W * H)) );
	}
	RenDev->RenderInterface.InvalidateHardwareState();
	debugf( NAME_Init, TEXT("UC2GLDrv: shadowcheck summary: branch=%s, %i shadow textures checked, %i within 2/255 of the recomputed filter (largest error %.2f)"),
		Xbox ? TEXT("xbox") : TEXT("pc"), Checked, Good, WorstError );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
