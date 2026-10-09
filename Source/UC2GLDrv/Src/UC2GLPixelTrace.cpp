/*=============================================================================
	UC2GLPixelTrace.cpp: "what is drawn under this pixel" (debug only; fx-fixes,
	docs/porting/analysis/fx-fixes.md). Not Epic's.

	Console: GLPIXTRACE <x> <y> [all]   (x, y: pixels of the main framebuffer, top-left
	origin). During the next whole frame every pass of every draw is logged that changes
	the main framebuffer's pixel (x,y) ("all": every pass of every draw, into any target):

	  UC2GLDrv: pixtrace #<n> pass <p>/<passes> <target> pp=<stage> <material class> <material>
	             blend=<on/off> <src>,<dst> op=<op> atest=<ref or -> z=<test><write> cw=<mask>
	             ps=<pixel shader or ff> vs=<unique vertex shader> stages=<n> before=<RGBA> after=<RGBA>
	  UC2GLDrv: pixtrace #<n>   stage <s>: tex=<GL id> <w>x<h> cube=<0/1> rt=<0/1> tci=<index>
	             colorop=<op> <arg1>,<arg2> alphaop=<op> <arg1>,<arg2> xform=<0/1>

	Every pass that draws into a render target during the accumulation stage of the
	distortion (PP_STAGE_ACCUMULATE) also logs the whole target after it: how many texels hold
	distortion (alpha > 0) and their mean and largest red/green (the accumulated offsets,
	DistortionAccum.psh), and writes it as PixTraceAccum-<frame>-<n>.ppm in the working folder.

	Quiet unless armed: two calls from DrawPrimitive, which return at once.
=============================================================================*/

#include "UC2GLDrv.h"

static INT	GPixTraceFrame	= -1;	// the device frame being traced (-1: off)
static INT	GPixTraceX		= 0,
			GPixTraceY		= 0;
static UBOOL GPixTraceAll	= 0;
static INT	GPixTraceDraw	= 0;

void UC2GL_PixelTraceArm( UUC2GLRenderDevice* RenDev, INT X, INT Y, UBOOL All )
{
	GPixTraceFrame	= RenDev->FrameCounter + 1;
	GPixTraceX		= X;
	GPixTraceY		= Y;
	GPixTraceAll	= All;
	GPixTraceDraw	= 0;
	debugf( NAME_Init, TEXT("UC2GLDrv: pixtrace armed for frame %i at (%i,%i)%s"), GPixTraceFrame, X, Y, All ? TEXT(", every pass") : TEXT("") );
}

UBOOL UC2GL_PixelTraceActive( UUC2GLRenderDevice* RenDev )
{
	return GPixTraceFrame >= 0 && RenDev->FrameCounter == GPixTraceFrame;
}

// The traced pixel in the current target: the main framebuffer is stored bottom row first,
// render targets top row first (UC2GLPostProcess.cpp, conventions 1 and 2), scaled to their size.
static UBOOL TracedPixel( FOpenGLRenderInterface* RI, INT& X, INT& Y )
{
	UUC2GLRenderDevice*			RenDev	= RI->RenDev;
	FOpenGLRenderTargetState*	RT		= RI->SavedRenderTargetState.GetCurrentState();
	INT		MainW = Max<INT>( RenDev->MainWidth, 1 ), MainH = Max<INT>( RenDev->MainHeight, 1 );
	if( RT->Framebuffer == RenDev->MainFramebuffer || !RT->RenderTarget )
	{
		X = GPixTraceX;
		Y = MainH - 1 - GPixTraceY;
		return 1;
	}
	X = GPixTraceX * RT->Width / MainW;
	Y = GPixTraceY * RT->Height / MainH;
	if( !RT->Flipped )
		Y = RT->Height - 1 - Y;
	return 0;
}

void UC2GL_PixelTraceRead( FOpenGLRenderInterface* RI, FColor& Out )
{
	INT X, Y;
	TracedPixel( RI, X, Y );
	BYTE RGBA[4] = { 0, 0, 0, 0 };
	// M11 (O-8): a multisampled main framebuffer cannot be read: read its resolve, made now.
	UUC2GLRenderDevice* RenDev = RI->RenDev;
	UBOOL Resolved = RenDev->MainSamples && RI->SavedRenderTargetState.GetCurrentState()->Framebuffer == RenDev->MainFramebuffer;
	if( Resolved )
	{
		RenDev->ResolvedColorVersion = -1;
		RenDev->ResolveMain( 1, 0 );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainResolveFramebuffer );
	}
	RI->RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	RI->RenDev->glReadPixels( X, Y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, RGBA );
	if( Resolved )
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );
	Out = FColor( RGBA[0], RGBA[1], RGBA[2], RGBA[3] );
}

static const TCHAR* BlendName( DWORD B )
{
	switch( B )
	{
	case D3DBLEND_ZERO:			return TEXT("0");
	case D3DBLEND_ONE:			return TEXT("1");
	case D3DBLEND_SRCCOLOR:		return TEXT("sc");
	case D3DBLEND_INVSRCCOLOR:	return TEXT("1-sc");
	case D3DBLEND_SRCALPHA:		return TEXT("sa");
	case D3DBLEND_INVSRCALPHA:	return TEXT("1-sa");
	case D3DBLEND_DESTALPHA:	return TEXT("da");
	case D3DBLEND_INVDESTALPHA:	return TEXT("1-da");
	case D3DBLEND_DESTCOLOR:	return TEXT("dc");
	case D3DBLEND_INVDESTCOLOR:	return TEXT("1-dc");
	default:					return TEXT("?");
	}
}

void UC2GL_PixelTraceLog( FOpenGLRenderInterface* RI, INT Pass, const FColor& Before, const FColor& After )
{
	INT		X, Y;
	UBOOL	Main = TracedPixel( RI, X, Y );
	GPixTraceDraw++;
	if( !GPixTraceAll && (!Main || Before == After) )
		return;
	FOpenGLMaterialState*		MS	= RI->CurrentState->MaterialPasses[Pass];
	FOpenGLRenderTargetState*	RT	= RI->SavedRenderTargetState.GetCurrentState();
	UMaterial*					M	= RI->CurrentState->LastMaterial;
	debugf( NAME_Init, TEXT("UC2GLDrv: pixtrace #%i pass %i/%i %s %ix%i pp=%i %s %s blend=%i %s,%s op=%i atest=%i/%i z=%i%i cw=%x ps=%i(%s) vs=%llx stages=%i before=%i,%i,%i,%i after=%i,%i,%i,%i"),
		GPixTraceDraw, Pass, RI->CurrentState->NumMaterialPasses, Main ? TEXT("main") : TEXT("target"), RT->Width, RT->Height,
		(INT)RI->PostProcessStage, M ? M->GetClass()->GetName() : TEXT("None"), M ? M->GetPathName() : TEXT("None"),
		MS->AlphaBlending, BlendName(MS->SrcBlend), BlendName(MS->DestBlend), (INT)MS->BlendOp, MS->AlphaTest, MS->AlphaRef, MS->ZTest, MS->ZWrite,
		MS->ColorWriteMask, (INT)MS->PixelShader, MS->UsePixelShaderProgram ? TEXT("program") : TEXT("ff"), (QWORD)MS->MaterialVertexShaderEnum, MS->StagesUsed,
		Before.R, Before.G, Before.B, Before.A, After.R, After.G, After.B, After.A );
	if( !Main && RI->PostProcessStage == FRenderInterface::PP_STAGE_ACCUMULATE && RT->Width > 0 && RT->Height > 0 )
	{
		TArray<BYTE> Pixels( RT->Width * RT->Height * 4 );
		RI->RenDev->glReadPixels( 0, 0, RT->Width, RT->Height, GL_RGBA, GL_UNSIGNED_BYTE, &Pixels(0) );
		INT Count = 0, Over16 = 0, MaxR = 0, MaxG = 0;
		DOUBLE SumR = 0, SumG = 0;
		for( INT i=0; i<RT->Width*RT->Height; i++ )
		{
			BYTE* P = &Pixels(i*4);
			if( !P[3] )
				continue;
			Count++;
			SumR += P[0]; SumG += P[1];
			MaxR = Max<INT>( MaxR, P[0] ); MaxG = Max<INT>( MaxG, P[1] );
			if( P[0] > 16 || P[1] > 16 )
				Over16++;
		}
		debugf( NAME_Init, TEXT("UC2GLDrv: pixtrace #%i   accumulation %ix%i: %i texels with distortion, mean r,g %.2f,%.2f, max %i,%i, %i over 16"),
			GPixTraceDraw, RT->Width, RT->Height, Count, Count ? SumR/Count : 0.0, Count ? SumG/Count : 0.0, MaxR, MaxG, Over16 );
		FString Name = FString::Printf( TEXT("PixTraceAccum-%i-%i.ppm"), GPixTraceFrame, GPixTraceDraw );
		FArchive* Ar = GFileManager->CreateFileWriter( *Name );
		if( Ar )
		{
			FString Header = FString::Printf( TEXT("P6\n%i %i\n255\n"), RT->Width, RT->Height );
			Ar->Serialize( (void*)TCHAR_TO_ANSI(*Header), Header.Len() );
			// Top row first (render targets are stored that way, UC2GLPostProcess.cpp).
			for( INT y=0; y<RT->Height; y++ )
				for( INT x=0; x<RT->Width; x++ )
					Ar->Serialize( &Pixels((y*RT->Width + x)*4), 3 );
			delete Ar;
		}
	}
	for( INT s=0; s<MS->StagesUsed && s<MAX_STAGES; s++ )
	{
		FOpenGLMaterialStateStage& St = MS->Stages[s];
		debugf( NAME_Init, TEXT("UC2GLDrv: pixtrace #%i   stage %i: tex=%i %ix%i mips=%i cube=%i rt=%i tci=%x tcc=%x colorop=%i %x,%x alphaop=%i %x,%x xform=%i lodbias=%.2f filter=%i/%i/%i"),
			GPixTraceDraw, s, St.Texture ? (INT)St.Texture->TextureID : -1, St.Texture ? St.Texture->CachedWidth : 0, St.Texture ? St.Texture->CachedHeight : 0,
			St.Texture ? St.Texture->CachedNumMips - St.Texture->CachedFirstMip : 0,
			St.Texture ? St.Texture->IsCubemap : 0, St.Texture ? St.Texture->IsRenderTarget : 0, St.TexCoordIndex, St.TexCoordCount,
			(INT)St.ColorOp, St.ColorArg1, St.ColorArg2, (INT)St.AlphaOp, St.AlphaArg1, St.AlphaArg2, St.TextureTransformsEnabled,
			RI->RenDev->NoLODBias ? 0.f : St.TextureMipLODBias, (INT)St.MinFilter, (INT)St.MagFilter, (INT)St.MipFilter );
	}
}

/*-----------------------------------------------------------------------------
	O-9: material census (test key MatCensus=True). Every distinct compilation of a material is
	logged once, as the material compiler left it: passes, blending, and per stage the
	colour and alpha operation with their arguments and the texture target. Comparing the
	census of two runs (e.g. 8 stages and exact operations against 4 stages without
	three-operand operations) lists the materials whose compilation depends on the device.

	  UC2GLDrv: census <material> [-> fallback <material>] passes=<n> | p<i> blend=<0/1> <src>,<dst>
	             ps=<pixel shader>(<program/ff>) stages=<n>: c<op>(<arg1>,<arg2>,<arg0>) a<op>(...) <2d/cube/->; ...
-----------------------------------------------------------------------------*/

static INT GMatCensus = -1;

// The compiled passes of the current material state, as one line.
static FString UC2GL_PassSignature( FOpenGLRenderInterface* RI )
{
	FString Line = FString::Printf( TEXT("passes=%i"), RI->CurrentState->NumMaterialPasses );
	for( INT p=0; p<RI->CurrentState->NumMaterialPasses; p++ )
	{
		FOpenGLMaterialState* MS = RI->CurrentState->MaterialPasses[p];
		if( !MS )
			continue;
		Line += FString::Printf( TEXT(" | p%i blend=%i %s,%s ps=%i(%s) stages=%i:"), p, MS->AlphaBlending, BlendName(MS->SrcBlend), BlendName(MS->DestBlend),
			(INT)MS->PixelShader, MS->UsePixelShaderProgram ? TEXT("program") : TEXT("ff"), MS->StagesUsed );
		for( INT s=0; s<MS->StagesUsed && s<MAX_STAGES; s++ )
		{
			FOpenGLMaterialStateStage& St = MS->Stages[s];
			Line += FString::Printf( TEXT(" c%i(%x,%x,%x) a%i(%x,%x,%x) %s;"), (INT)St.ColorOp, St.ColorArg1, St.ColorArg2, St.ColorArg0,
				(INT)St.AlphaOp, St.AlphaArg1, St.AlphaArg2, St.AlphaArg0,
				!St.Texture ? TEXT("-") : St.Texture->IsCubemap ? TEXT("cube") : TEXT("2d") );
		}
	}
	return Line;
}

void UC2GL_MaterialCensus( FOpenGLRenderInterface* RI, UMaterial* Requested, UMaterial* Used )
{
	extern UBOOL GTestMatCensus;	// test key MatCensus=True (UC2GLRenderDevice.cpp Init)
	if( GMatCensus < 0 )
		GMatCensus = GTestMatCensus ? 1 : 0;
	if( GMatCensus != 1 || !Requested )
		return;
	static TMap<FString,INT> Seen;
	if( Seen.Num() >= 20000 )
		return;
	FString Line = FString::Printf( TEXT("%s"), Requested->GetPathName() );
	if( Used != Requested )
		Line += FString::Printf( TEXT(" -> fallback %s"), Used ? Used->GetPathName() : TEXT("None") );
	Line += TEXT(" ");
	Line += UC2GL_PassSignature( RI );
	if( Seen.Find( Line ) )
		return;
	Seen.Set( *Line, 1 );
	debugf( NAME_Init, TEXT("UC2GLDrv: census %s"), *Line );
}

/*
	GLMATCENSUS (console; runs at the start of the next frame): every material object in
	memory is compiled, lit and unlit, with three sets of device caps, and nothing is drawn:
	  A  8 stages, MODULATEALPHA_ADDCOLOR and MULTIPLYADD (fragment programs; Mesa before O-9)
	  B  4 stages, the same ops (Epic's D3DDrv on every card it accepted, and the Xbox)
	  C  4 stages, neither op (the NVIDIA driver before O-9)
	Logged: every material whose passes differ between A and B or between B and C, with the
	three compilations, and a summary. Pixel-shader, projector, terrain, particle and movie
	materials are skipped (they are not plain fixed-function materials or need a draw context).
*/
void UC2GL_RunMaterialCensus( UUC2GLRenderDevice* RenDev, UViewport* Viewport )
{
	FOpenGLRenderInterface* RI = &RenDev->RenderInterface;
	struct { const TCHAR* Name; INT Stages; DWORD Caps; } Cfg[3] =
	{
		{ TEXT("A"), 8, D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR | D3DTEXOPCAPS_MULTIPLYADD },
		{ TEXT("B"), 4, D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR | D3DTEXOPCAPS_MULTIPLYADD },
		{ TEXT("C"), 4, 0 },
	};
	DWORD	SavedTex = RenDev->DeviceCaps.MaxSimultaneousTextures, SavedBlend = RenDev->DeviceCaps.MaxTextureBlendStages, SavedOps = RenDev->DeviceCaps.TextureOpCaps;
	INT		SavedUnits = RenDev->NumTextureUnits;
	INT		Materials = 0, DiffAB = 0, DiffBC = 0, FallbackA = 0, FallbackB = 0, FallbackC = 0;
	GMatCensus = 2;		// SetMaterial's own census stays quiet meanwhile
	RI->PushState();
	for( TObjectIterator<UMaterial> It; It; ++It )
	{
		UMaterial* M = *It;
		if( M->IsA(UPixelShaderMaterial::StaticClass()) || M->IsA(UProjectorMaterial::StaticClass()) || M->IsA(UProjectorMultiMaterial::StaticClass())
		||	M->IsA(UTerrainMaterial::StaticClass()) || M->IsA(UParticleMaterial::StaticClass()) || M->IsA(UMovieMaterial::StaticClass()) )
			continue;
		Materials++;
		for( INT Lit=0; Lit<2; Lit++ )
		{
			FString Sig[3];
			UBOOL	Fallback[3];
			for( INT c=0; c<3; c++ )
			{
				RenDev->DeviceCaps.MaxSimultaneousTextures	= Cfg[c].Stages;
				RenDev->DeviceCaps.MaxTextureBlendStages	= Cfg[c].Stages;
				RenDev->DeviceCaps.TextureOpCaps			= Cfg[c].Caps;
				RenDev->NumTextureUnits						= Cfg[c].Stages;
				RI->EnableLighting( Lit, Lit, 0, NULL, 0, FSphere(FVector(0,0,0),0.f) );
				FString Error;
				UMaterial* ErrorMaterial = NULL;
				RI->SetMaterial( M, &Error, &ErrorMaterial, NULL );
				Fallback[c] = Error.Len() > 0;
				Sig[c] = UC2GL_PassSignature( RI ) + (Fallback[c] ? FString::Printf( TEXT(" ERROR(%s)"), *Error ) : FString());
			}
			FallbackA += Fallback[0]; FallbackB += Fallback[1]; FallbackC += Fallback[2];
			UBOOL AB = Sig[0] != Sig[1], BC = Sig[1] != Sig[2];
			DiffAB += AB; DiffBC += BC;
			if( AB || BC )
				debugf( NAME_Init, TEXT("UC2GLDrv: matcensus %s %s %s%s%s\n    A: %s\n    B: %s\n    C: %s"), Lit ? TEXT("lit") : TEXT("unlit"), M->GetClass()->GetName(), M->GetPathName(),
					AB ? TEXT(" [A!=B: 8 vs 4 stages]") : TEXT(""), BC ? TEXT(" [B!=C: the three-operand caps]") : TEXT(""), *Sig[0], *Sig[1], *Sig[2] );
		}
	}
	RI->PopState();
	RenDev->DeviceCaps.MaxSimultaneousTextures	= SavedTex;
	RenDev->DeviceCaps.MaxTextureBlendStages	= SavedBlend;
	RenDev->DeviceCaps.TextureOpCaps			= SavedOps;
	RenDev->NumTextureUnits						= SavedUnits;
	RI->InvalidateHardwareState();
	GMatCensus = -1;
	debugf( NAME_Init, TEXT("UC2GLDrv: matcensus summary: %i materials x lit/unlit; differ A/B (8 vs 4 stages) %i, B/C (three-operand caps) %i; compile errors A %i, B %i, C %i"),
		Materials, DiffAB, DiffBC, FallbackA, FallbackB, FallbackC );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
