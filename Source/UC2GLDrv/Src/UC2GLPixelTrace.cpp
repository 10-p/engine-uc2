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
	RI->RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	RI->RenDev->glReadPixels( X, Y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, RGBA );
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
	for( INT s=0; s<MS->StagesUsed && s<MAX_STAGES; s++ )
	{
		FOpenGLMaterialStateStage& St = MS->Stages[s];
		debugf( NAME_Init, TEXT("UC2GLDrv: pixtrace #%i   stage %i: tex=%i %ix%i cube=%i rt=%i tci=%x tcc=%x colorop=%i %x,%x alphaop=%i %x,%x xform=%i"),
			GPixTraceDraw, s, St.Texture ? (INT)St.Texture->TextureID : -1, St.Texture ? St.Texture->CachedWidth : 0, St.Texture ? St.Texture->CachedHeight : 0,
			St.Texture ? St.Texture->IsCubemap : 0, St.Texture ? St.Texture->IsRenderTarget : 0, St.TexCoordIndex, St.TexCoordCount,
			(INT)St.ColorOp, St.ColorArg1, St.ColorArg2, (INT)St.AlphaOp, St.AlphaArg1, St.AlphaArg2, St.TextureTransformsEnabled );
	}
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
