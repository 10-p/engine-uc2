/*=============================================================================
	UC2GLCombinerTest.cpp: numeric test of the texture stages (O-9). Not Epic's.

	Every Direct3D 8 texture operation the material compiler (UC2GLMaterialState.cpp)
	emits, and the others the GL translation knows, is drawn through the driver's own
	path for fixed-function passes (SetMaterialBlending, then the fragment program of
	UC2GLFFProgram.cpp when FixedFunctionPrograms is on, GL's texture environment when it
	is off) with known textures, diffuse, specular and texture-factor colours, read back and
	compared with a CPU computation of the operation's definition in the DirectX 8 SDK:

	  SELECTARG1 a1, SELECTARG2 a2, MODULATE a1*a2 (2X, 4X: times 2, 4), ADD a1+a2,
	  ADDSIGNED a1+a2-0.5 (2X: times 2), SUBTRACT a1-a2, ADDSMOOTH a1+a2-a1*a2,
	  BLEND{DIFFUSE,TEXTURE,FACTOR,CURRENT}ALPHA a1*A+a2*(1-A), BLENDTEXTUREALPHAPM a1+a2*(1-Atex),
	  MODULATEALPHA_ADDCOLOR a1+a1.A*a2, MODULATECOLOR_ADDALPHA a1*a2+a1.A,
	  MODULATEINVALPHA_ADDCOLOR (1-a1.A)*a2+a1, MODULATEINVCOLOR_ADDALPHA (1-a1)*a2+a1.A,
	  DOTPRODUCT3 4*((a1.r-.5)(a2.r-.5)+...) into all channels, MULTIPLYADD a0+a1*a2,
	  LERP a0*a1+(1-a0)*a2; arguments with COMPLEMENT (1-x) and ALPHAREPLICATE (x.aaaa);
	  every stage result saturated to [0,1].

	Also: eight stages in one pass, the colour sum (secondary colour), linear fog, a
	projected 2D lookup (s/q, t/q), cube-map face selection and the per-stage LOD bias
	(on stage 0 and stage 5). A case that needs more stages than the device reports is
	SKIPPED, with that said on its line.

	Console: COMBINERTEST (next frame). Command line: -glcombinertest (tenth frame with a
	player). Lines: "UC2GLDrv: combinertest <case>: PASS|FAIL|SKIP ..." and a summary line
	naming the path (fragment programs / texture environment with combine3, combine4 or
	neither) and the number of stages. Drawn into a 4x4 framebuffer of its own.
=============================================================================*/

#include "UC2GLDrv.h"

struct FCTStage
{
	INT		ColorOp;
	DWORD	CA1, CA2, CA0;
	INT		AlphaOp;
	DWORD	AA1, AA2, AA0;
};

enum
{
	CTF_ColorSum	= 1,
	CTF_Fog			= 2,
	CTF_Projected	= 4,	// stage 0's coordinates carry q = 2
	CTF_FogFront	= 8,	// fog with the quad at eye z +0.5 (D3D's camera space, which the driver's modelview is) instead of -0.5
};

struct FCTCase
{
	const TCHAR*	Name;
	INT				NumStages;
	DWORD			Flags;
	FCTStage		Stages[MAX_STAGES];
};

#define TA_D	D3DTA_DIFFUSE
#define TA_C	D3DTA_CURRENT
#define TA_T	D3DTA_TEXTURE
#define TA_F	D3DTA_TFACTOR
#define TA_S	D3DTA_SPECULAR
#define TA_INV	D3DTA_COMPLEMENT
#define TA_REP	D3DTA_ALPHAREPLICATE

// Stage 0 of most cases: CURRENT = texture * diffuse (colour and alpha), so that CURRENT,
// TEXTURE, DIFFUSE and TFACTOR all differ at stage 1.
#define BASE	{ D3DTOP_MODULATE, TA_T, TA_D, 0, D3DTOP_MODULATE, TA_T, TA_D, 0 }
#define KEEPA	D3DTOP_SELECTARG1, TA_C, 0, 0

static const FCTCase GCTCases[] =
{
	{ TEXT("selectarg1"),				2, 0, { BASE, { D3DTOP_SELECTARG1, TA_T, 0, 0, D3DTOP_SELECTARG1, TA_T, 0, 0 } } },
	{ TEXT("selectarg2-complement"),	2, 0, { BASE, { D3DTOP_SELECTARG2, TA_T, TA_C|TA_INV, 0, D3DTOP_SELECTARG2, TA_T, TA_C|TA_INV, 0 } } },
	{ TEXT("modulate"),					2, 0, { BASE, { D3DTOP_MODULATE, TA_T, TA_C, 0, D3DTOP_MODULATE, TA_T, TA_C, 0 } } },
	{ TEXT("modulate2x"),				2, 0, { BASE, { D3DTOP_MODULATE2X, TA_T, TA_D, 0, D3DTOP_MODULATE2X, TA_T, TA_D, 0 } } },
	{ TEXT("modulate4x"),				2, 0, { BASE, { D3DTOP_MODULATE4X, TA_T, TA_F, 0, D3DTOP_MODULATE4X, TA_T, TA_F, 0 } } },
	{ TEXT("add"),						2, 0, { BASE, { D3DTOP_ADD, TA_T, TA_C, 0, D3DTOP_ADD, TA_T, TA_C, 0 } } },
	{ TEXT("addsigned"),				2, 0, { BASE, { D3DTOP_ADDSIGNED, TA_T, TA_C, 0, D3DTOP_ADDSIGNED, TA_T, TA_C, 0 } } },
	{ TEXT("addsigned2x"),				2, 0, { BASE, { D3DTOP_ADDSIGNED2X, TA_T, TA_C, 0, D3DTOP_ADDSIGNED2X, TA_T, TA_C, 0 } } },
	{ TEXT("subtract"),					2, 0, { BASE, { D3DTOP_SUBTRACT, TA_T, TA_C, 0, D3DTOP_SUBTRACT, TA_T, TA_C, 0 } } },
	{ TEXT("addsmooth"),				2, 0, { BASE, { D3DTOP_ADDSMOOTH, TA_T, TA_C, 0, D3DTOP_ADDSMOOTH, TA_T, TA_C, 0 } } },
	{ TEXT("blenddiffusealpha"),		2, 0, { BASE, { D3DTOP_BLENDDIFFUSEALPHA, TA_T, TA_C, 0, D3DTOP_BLENDDIFFUSEALPHA, TA_T, TA_C, 0 } } },
	{ TEXT("blendtexturealpha"),		2, 0, { BASE, { D3DTOP_BLENDTEXTUREALPHA, TA_T, TA_C, 0, KEEPA } } },
	{ TEXT("blendfactoralpha"),			2, 0, { BASE, { D3DTOP_BLENDFACTORALPHA, TA_T, TA_C, 0, D3DTOP_BLENDFACTORALPHA, TA_T, TA_C, 0 } } },
	{ TEXT("blendcurrentalpha"),		2, 0, { BASE, { D3DTOP_BLENDCURRENTALPHA, TA_T, TA_D, 0, KEEPA } } },
	{ TEXT("blendtexturealphapm"),		2, 0, { BASE, { D3DTOP_BLENDTEXTUREALPHAPM, TA_T, TA_C, 0, KEEPA } } },
	// UC2GLMaterialState.cpp 791 (specular stage, per-pixel specularity) and 884 (AddToCurrent):
	{ TEXT("modulatealpha_addcolor"),	2, 0, { BASE, { D3DTOP_MODULATEALPHA_ADDCOLOR, TA_C, TA_T, 0, KEEPA } } },
	// 1094 (Combiner CO_Add_With_Mask_Modulation, not swapped):
	{ TEXT("modulatealpha_addcolor-2"),	2, 0, { BASE, { D3DTOP_MODULATEALPHA_ADDCOLOR, TA_T, TA_C, 0, D3DTOP_SELECTARG1, TA_T, 0, 0 } } },
	{ TEXT("modulatecolor_addalpha"),	2, 0, { BASE, { D3DTOP_MODULATECOLOR_ADDALPHA, TA_C, TA_T, 0, KEEPA } } },
	{ TEXT("modulateinvalpha_addcolor"),2, 0, { BASE, { D3DTOP_MODULATEINVALPHA_ADDCOLOR, TA_C, TA_T, 0, KEEPA } } },
	{ TEXT("modulateinvcolor_addalpha"),2, 0, { BASE, { D3DTOP_MODULATEINVCOLOR_ADDALPHA, TA_C, TA_T, 0, KEEPA } } },
	// 771 (UseDiffuseModulate) and 785 (UseConstantSpecularity):
	{ TEXT("multiplyadd-diffuse"),		2, 0, { BASE, { D3DTOP_MULTIPLYADD, TA_D, TA_T, TA_C, KEEPA } } },
	{ TEXT("multiplyadd-tfactor"),		2, 0, { BASE, { D3DTOP_MULTIPLYADD, TA_F, TA_T, TA_C, KEEPA } } },
	{ TEXT("multiplyadd-alpha"),		2, 0, { BASE, { D3DTOP_SELECTARG1, TA_C, 0, 0, D3DTOP_MULTIPLYADD, TA_T, TA_D, TA_C } } },
	{ TEXT("lerp"),						2, 0, { BASE, { D3DTOP_LERP, TA_C, TA_F, TA_T|TA_REP, D3DTOP_LERP, TA_C, TA_F, TA_T } } },
	{ TEXT("dotproduct3"),				2, 0, { BASE, { D3DTOP_DOTPRODUCT3, TA_T, TA_C, 0, KEEPA } } },
	{ TEXT("modifiers"),				2, 0, { BASE, { D3DTOP_MODULATE, TA_T|TA_REP, TA_C|TA_INV, 0, D3DTOP_MODULATE, TA_T|TA_INV, TA_D, 0 } } },
	// The terrain weight-map stage (current * texture alpha) and a three-stage chain like a Shader's.
	{ TEXT("chain-3"),					3, 0, { BASE, { D3DTOP_MODULATE, TA_C, TA_T|TA_REP, 0, KEEPA }, { D3DTOP_MODULATEALPHA_ADDCOLOR, TA_C, TA_T, 0, D3DTOP_MODULATE, TA_C, TA_T, 0 } } },
	{ TEXT("eight-stages"),				8, 0, { BASE,
		{ D3DTOP_ADD, TA_T, TA_C, 0, D3DTOP_MODULATE, TA_T, TA_C, 0 },
		{ D3DTOP_MODULATE, TA_T, TA_C, 0, D3DTOP_ADD, TA_T, TA_C, 0 },
		{ D3DTOP_BLENDTEXTUREALPHA, TA_T, TA_C, 0, D3DTOP_SELECTARG1, TA_T, 0, 0 },
		{ D3DTOP_MODULATEALPHA_ADDCOLOR, TA_C, TA_T, 0, D3DTOP_MODULATE, TA_T, TA_C, 0 },
		{ D3DTOP_SUBTRACT, TA_C, TA_T, 0, D3DTOP_ADD, TA_C, TA_T, 0 },
		{ D3DTOP_MULTIPLYADD, TA_D, TA_T, TA_C, D3DTOP_MODULATE, TA_C, TA_F, 0 },
		{ D3DTOP_MODULATE2X, TA_C, TA_T, 0, D3DTOP_SELECTARG1, TA_C, 0, 0 } } },
	{ TEXT("colorsum"),					2, CTF_ColorSum, { BASE, { D3DTOP_SELECTARG1, TA_T, 0, 0, D3DTOP_SELECTARG1, TA_T, 0, 0 } } },
	{ TEXT("fog"),						1, CTF_Fog, { BASE } },
	{ TEXT("fog-front"),				1, CTF_Fog|CTF_FogFront, { BASE } },
	{ TEXT("projected"),				1, CTF_Projected, { { D3DTOP_SELECTARG1, TA_T, 0, 0, D3DTOP_SELECTARG1, TA_T, 0, 0 } } },
};

static FLOAT CTSat( FLOAT X ) { return Clamp( X, 0.f, 1.f ); }

struct FUC2GLCombinerTest
{
	UUC2GLRenderDevice*		RenDev;
	FOpenGLRenderInterface*	RI;
	INT						Passed, Failed, Skipped;
	FOpenGLTexture*			Tex[MAX_STAGES];
	BYTE					TexData[MAX_STAGES][16*4];
	FOpenGLTexture*			Cube;
	FOpenGLTexture*			Mip;
	GLuint					Framebuffer, Target, Buffer;

	// Colours of the three passes of every case (R,G,B,A bytes).
	BYTE					Diffuse[3][4], Factor[3][4], Specular[3][4];

	void Report( const TCHAR* Name, INT Compared, FLOAT MaxError, FLOAT Tolerance, const TCHAR* Detail )
	{
		UBOOL Ok = Compared > 0 && MaxError <= Tolerance;
		if( Ok ) Passed++; else Failed++;
		debugf( NAME_Init, TEXT("UC2GLDrv: combinertest %s: %s, %i values compared, largest error %.2f of 255 (tolerance %.2f)%s%s"),
			Name, Ok ? TEXT("PASS") : TEXT("FAIL"), Compared, MaxError, Tolerance, *Detail ? TEXT("; ") : TEXT(""), Detail );
	}
	void Skip( const TCHAR* Name, const TCHAR* Why )
	{
		Skipped++;
		debugf( NAME_Init, TEXT("UC2GLDrv: combinertest %s: SKIP, %s"), Name, Why );
	}

	FOpenGLTexture* NewTexture( UBOOL IsCube, INT NumMips )
	{
		FOpenGLTexture* T = new FOpenGLTexture( RenDev, NULL );
		RenDev->glGenTextures( 1, &T->TextureID );
		T->IsCubemap		= IsCube;
		T->CachedWidth		= IsCube ? 1 : 4;
		T->CachedHeight		= IsCube ? 1 : 4;
		T->CachedFirstMip	= 0;
		T->CachedNumMips	= NumMips;
		return T;
	}

	void Setup()
	{
		RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		for( INT s=0; s<MAX_STAGES; s++ )
		{
			for( INT i=0; i<16*4; i++ )
			{
				DWORD H = (DWORD)(i * 2654435761u) ^ (DWORD)((s + 1) * 40503u);
				H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
				TexData[s][i] = (BYTE)H;
			}
			// Corners at the extremes: saturation and the 0.5 offsets of the signed operations.
			for( INT c=0; c<4; c++ )
			{
				TexData[s][0*4+c] = 0;
				TexData[s][15*4+c] = 255;
				TexData[s][5*4+c] = 128;
			}
			Tex[s] = NewTexture( 0, 1 );
			RenDev->glBindTexture( GL_TEXTURE_2D, Tex[s]->TextureID );
			RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, TexData[s] );
		}
		// Cube map: one texel per face, a different colour each.
		Cube = NewTexture( 1, 1 );
		RenDev->glBindTexture( GL_TEXTURE_CUBE_MAP, Cube->TextureID );
		for( INT f=0; f<6; f++ )
		{
			BYTE Texel[4] = { (BYTE)(40 * f + 10), (BYTE)(250 - 37 * f), (BYTE)(f & 1 ? 200 : 60), (BYTE)(255 - 20 * f) };
			RenDev->glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texel );
		}
		RenDev->glBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
		// Three mip levels of one colour each: which one a lookup takes shows its LOD.
		Mip = NewTexture( 0, 3 );
		RenDev->glBindTexture( GL_TEXTURE_2D, Mip->TextureID );
		if( RenDev->IsGLES )
		{
			// GL4ES' record of the texture first, as FOpenGLTexture::PrepareStorage gives every engine
			// texture: one texel, mipmaps "taken care of"; the levels themselves go around it below.
			static const BYTE Texel[4] = { 255, 255, 255, 255 };
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE );
			RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texel );
		}
		for( INT l=0; l<3; l++ )
		{
			INT		Size = 4 >> l;
			BYTE	Level[16*4];
			for( INT i=0; i<Size*Size; i++ )
			{
				Level[i*4+0] = (BYTE)(l == 0 ? 255 : 0);
				Level[i*4+1] = (BYTE)(l == 1 ? 255 : 0);
				Level[i*4+2] = (BYTE)(l == 2 ? 255 : 0);
				Level[i*4+3] = 255;
			}
			// (Through the driver's storage helper, as engine textures: on GLES it goes around GL4ES,
			// which would replace levels 1 and 2 by a chain generated from level 0. F-3.)
			RenDev->TexImage2D( GL_TEXTURE_2D, Mip->TextureID, GL_TEXTURE_2D, l, GL_RGBA, Size, Size, GL_RGBA, GL_UNSIGNED_BYTE, Level );
		}
		if( !RenDev->IsGLES )
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2 );
		RenDev->glBindTexture( GL_TEXTURE_2D, 0 );

		static const BYTE D[3][4] = { { 204, 128, 51, 153 }, { 255, 255, 255, 255 }, { 32, 200, 100, 30 } },
						  F[3][4] = { { 77, 230, 128, 64 },  { 0, 0, 0, 0 },         { 250, 10, 180, 200 } },
						  S[3][4] = { { 40, 80, 120, 255 },  { 200, 0, 90, 255 },    { 0, 255, 30, 255 } };
		appMemcpy( Diffuse, D, sizeof(D) );
		appMemcpy( Factor, F, sizeof(F) );
		appMemcpy( Specular, S, sizeof(S) );

		// A 4x4 target of our own (independent of the main framebuffer's anti-aliasing).
		RenDev->glGenTextures( 1, &Target );
		RenDev->glBindTexture( GL_TEXTURE_2D, Target );
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		RenDev->glBindTexture( GL_TEXTURE_2D, 0 );
		RenDev->glGenFramebuffers( 1, &Framebuffer );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
		RenDev->AttachColorTexture( Target );
		Buffer = RenDev->NewBuffer( GL_ARRAY_BUFFER );
	}

	void Teardown()
	{
		for( INT s=0; s<MAX_STAGES; s++ )
			delete Tex[s];
		delete Cube;
		delete Mip;
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RI->SavedRenderTargetState.GetCurrentState()->Framebuffer );
		RenDev->glDeleteFramebuffers( 1, &Framebuffer );
		RenDev->glDeleteTextures( 1, &Target );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, 0 );
		RenDev->FreeBuffer( GL_ARRAY_BUFFER, Buffer );
	}

	// One stage, every channel, by D3D's definitions. Cur is updated.
	void CPUStage( const FCTStage& St, INT s, const FLOAT* D, const FLOAT* F, const FLOAT* S, const FLOAT* T, FLOAT* Cur )
	{
		FLOAT Result[4];
		for( INT Half=0; Half<2; Half++ )
		{
			INT		Op		= Half ? St.AlphaOp : St.ColorOp;
			DWORD	Args[3]	= { Half ? St.AA0 : St.CA0, Half ? St.AA1 : St.CA1, Half ? St.AA2 : St.CA2 };
			// a[k][ch]: argument k (0 = Arg0) with its modifiers, channel ch.
			FLOAT	a[3][4];
			for( INT k=0; k<3; k++ )
			{
				const FLOAT* Base;
				static const FLOAT Zero[4] = { 0, 0, 0, 0 };
				switch( Args[k] & D3DTA_SELECTMASK )
				{
				case D3DTA_DIFFUSE:		Base = D;		break;
				case D3DTA_CURRENT:		Base = Cur;		break;
				case D3DTA_TEXTURE:		Base = T;		break;
				case D3DTA_TFACTOR:		Base = F;		break;
				case D3DTA_SPECULAR:	Base = S;		break;
				default:				Base = Zero;	break;
				}
				for( INT ch=0; ch<4; ch++ )
				{
					FLOAT v = (Args[k] & D3DTA_ALPHAREPLICATE) ? Base[3] : Base[ch];
					a[k][ch] = (Args[k] & D3DTA_COMPLEMENT) ? 1.f - v : v;
				}
			}
			for( INT ch = Half ? 3 : 0; ch < (Half ? 4 : 3); ch++ )
			{
				FLOAT a0 = a[0][ch], a1 = a[1][ch], a2 = a[2][ch], a1A = a[1][3], r;
				switch( Op )
				{
				case D3DTOP_SELECTARG1:					r = a1; break;
				case D3DTOP_SELECTARG2:					r = a2; break;
				case D3DTOP_MODULATE:					r = a1 * a2; break;
				case D3DTOP_MODULATE2X:					r = 2.f * a1 * a2; break;
				case D3DTOP_MODULATE4X:					r = 4.f * a1 * a2; break;
				case D3DTOP_ADD:						r = a1 + a2; break;
				case D3DTOP_ADDSIGNED:					r = a1 + a2 - 0.5f; break;
				case D3DTOP_ADDSIGNED2X:				r = 2.f * (a1 + a2 - 0.5f); break;
				case D3DTOP_SUBTRACT:					r = a1 - a2; break;
				case D3DTOP_ADDSMOOTH:					r = a1 + a2 - a1 * a2; break;
				case D3DTOP_BLENDDIFFUSEALPHA:			r = a1 * D[3] + a2 * (1.f - D[3]); break;
				case D3DTOP_BLENDTEXTUREALPHA:			r = a1 * T[3] + a2 * (1.f - T[3]); break;
				case D3DTOP_BLENDFACTORALPHA:			r = a1 * F[3] + a2 * (1.f - F[3]); break;
				case D3DTOP_BLENDCURRENTALPHA:			r = a1 * Cur[3] + a2 * (1.f - Cur[3]); break;
				case D3DTOP_BLENDTEXTUREALPHAPM:		r = a1 + a2 * (1.f - T[3]); break;
				case D3DTOP_MODULATEALPHA_ADDCOLOR:		r = a1 + a1A * a2; break;
				case D3DTOP_MODULATECOLOR_ADDALPHA:		r = a1 * a2 + a1A; break;
				case D3DTOP_MODULATEINVALPHA_ADDCOLOR:	r = (1.f - a1A) * a2 + a1; break;
				case D3DTOP_MODULATEINVCOLOR_ADDALPHA:	r = (1.f - a1) * a2 + a1A; break;
				case D3DTOP_MULTIPLYADD:				r = a0 + a1 * a2; break;
				case D3DTOP_LERP:						r = a0 * a1 + (1.f - a0) * a2; break;
				case D3DTOP_DOTPRODUCT3:				r = 4.f * ((a[1][0]-.5f)*(a[2][0]-.5f) + (a[1][1]-.5f)*(a[2][1]-.5f) + (a[1][2]-.5f)*(a[2][2]-.5f)); break;
				default:								r = Cur[ch]; break;
				}
				Result[ch] = CTSat( r );
			}
		}
		// DOTPRODUCT3 as the colour operation: the dot product goes into alpha as well.
		if( St.ColorOp == D3DTOP_DOTPRODUCT3 )
			Result[3] = Result[0];
		appMemcpy( Cur, Result, sizeof(Result) );
	}

	// Draw a 4x4 quad through the pass MS with the given colours; Coords: per vertex and
	// stage (s,t,r,q). Returns 0 if the device has no program for a pass it should program.
	UBOOL Draw( FOpenGLMaterialState& MS, const BYTE* D, const BYTE* S, UBOOL ColorSum, UBOOL Fog, const FLOAT Coords[4][MAX_STAGES][4], BYTE* Out, FLOAT FogZ=-0.5f )
	{
		// Vertex: position (3 floats), diffuse (4 bytes), specular (4 bytes), 8 x 4 texture coordinates.
		struct FVertex { FLOAT P[3]; BYTE D[4]; BYTE S[4]; FLOAT T[MAX_STAGES][4]; } V[4];
		static const FLOAT Corner[4][2] = { { -1.f, -1.f }, { 1.f, -1.f }, { -1.f, 1.f }, { 1.f, 1.f } };
		for( INT v=0; v<4; v++ )
		{
			V[v].P[0] = Corner[v][0]; V[v].P[1] = Corner[v][1]; V[v].P[2] = Fog ? FogZ : 0.f;
			appMemcpy( V[v].D, D, 4 );
			appMemcpy( V[v].S, S, 4 );
			appMemcpy( V[v].T, Coords[v], sizeof(V[v].T) );
		}
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
		RenDev->glViewport( 0, 0, 4, 4 );
		RenDev->glDisable( GL_SCISSOR_TEST );
		RenDev->glDisable( GL_STENCIL_TEST );
		RenDev->glDisable( GL_DEPTH_TEST );
		RenDev->glDisable( GL_LIGHTING );
		RenDev->glDisable( GL_COLOR_MATERIAL );
		RenDev->glClearColor( 0.f, 0.f, 0.f, 0.f );
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		RenDev->glClear( GL_COLOR_BUFFER_BIT );

		RI->CurrentState->CurrentMaterialState = NULL;
		RI->SetMaterialBlending( &MS, CM_None );
		RenDev->glDisable( GL_DEPTH_TEST );

		RenDev->glMatrixMode( GL_PROJECTION );
		RenDev->glLoadIdentity();
		RenDev->glMatrixMode( GL_MODELVIEW );
		RenDev->glLoadIdentity();
		if( Fog )
		{
			static const FLOAT FogColor[4] = { 1.f, 0.5f, 0.f, 1.f };
			RenDev->glEnable( GL_FOG );
			RenDev->glFogf( GL_FOG_START, 0.f );
			RenDev->glFogf( GL_FOG_END, 1.f );
			RenDev->glFogfv( GL_FOG_COLOR, FogColor );
		}
		else
			RenDev->glDisable( GL_FOG );
		if( ColorSum )
			RenDev->glEnable( GL_COLOR_SUM );
		else
			RenDev->glDisable( GL_COLOR_SUM );

		RenDev->glBindBuffer( GL_ARRAY_BUFFER, Buffer );
		RenDev->glBufferData( GL_ARRAY_BUFFER, sizeof(V), V, GL_STREAM_DRAW );
		RenDev->glVertexPointer( 3, GL_FLOAT, sizeof(FVertex), (const BYTE*)(PTRINT)STRUCT_OFFSET(FVertex,P) );
		RenDev->glEnableClientState( GL_VERTEX_ARRAY );
		RenDev->glDisableClientState( GL_NORMAL_ARRAY );
		RenDev->glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof(FVertex), (const BYTE*)(PTRINT)STRUCT_OFFSET(FVertex,D) );
		RenDev->glEnableClientState( GL_COLOR_ARRAY );
		if( ColorSum )
		{
			RenDev->glSecondaryColorPointer( 3, GL_UNSIGNED_BYTE, sizeof(FVertex), (const BYTE*)(PTRINT)STRUCT_OFFSET(FVertex,S) );
			RenDev->glEnableClientState( GL_SECONDARY_COLOR_ARRAY );
		}
		else
			RenDev->glDisableClientState( GL_SECONDARY_COLOR_ARRAY );
		for( INT s=0; s<Min<INT>(RenDev->GLProgramUnits,MAX_STAGES); s++ )
		{
			RenDev->glActiveTexture( GL_TEXTURE0 + s );
			RenDev->glClientActiveTexture( GL_TEXTURE0 + s );
			RenDev->glDisable( GL_TEXTURE_GEN_S );
			RenDev->glDisable( GL_TEXTURE_GEN_T );
			RenDev->glDisable( GL_TEXTURE_GEN_R );
			RenDev->glDisable( GL_TEXTURE_GEN_Q );
			RenDev->glMatrixMode( GL_TEXTURE );
			RenDev->glLoadIdentity();
			if( s < MS.StagesUsed )
			{
				RenDev->glTexCoordPointer( 4, GL_FLOAT, sizeof(FVertex), (const BYTE*)(PTRINT)(STRUCT_OFFSET(FVertex,T) + s * 4 * sizeof(FLOAT)) );
				RenDev->glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			}
			else
				RenDev->glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		}
		RenDev->glMatrixMode( GL_MODELVIEW );
		RenDev->glActiveTexture( GL_TEXTURE0 );
		RenDev->glClientActiveTexture( GL_TEXTURE0 );

		UBOOL Programmed = RI->BeginFixedFunctionProgram( &MS, ColorSum, Fog );
		UBOOL Ok = Programmed || !RenDev->FFProgramsActive;
		RenDev->glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		if( Programmed )
			RI->EndFixedFunctionProgram();

		RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		RenDev->glReadPixels( 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, Out );

		RenDev->glDisableClientState( GL_SECONDARY_COLOR_ARRAY );
		RenDev->glDisable( GL_COLOR_SUM );
		RenDev->glDisable( GL_FOG );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, 0 );
		return Ok;
	}

	void InitPass( FOpenGLMaterialState& MS, INT NumStages, BYTE* TFactor )
	{
		MS = RI->DefaultPass;
		MS.AlphaBlending	= 0;
		MS.AlphaTest		= 0;
		MS.ZTest			= 0;
		MS.ZWrite			= 0;
		MS.TwoSided			= 1;
		MS.FillMode			= FM_Solid;
		MS.PixelShader		= PS_None;
		MS.UsePixelShaderProgram = 0;
		MS.SpecularEnable	= 0;
		MS.ColorWriteMask	= CWM_COLORWRITEENABLE_RED | CWM_COLORWRITEENABLE_GREEN | CWM_COLORWRITEENABLE_BLUE | CWM_COLORWRITEENABLE_ALPHA;
		MS.TFactorColor		= TFactor ? FColor( TFactor[0], TFactor[1], TFactor[2], TFactor[3] ).DWColor() : 0;
		MS.StagesUsed		= NumStages;
		for( INT s=0; s<MAX_STAGES; s++ )
		{
			FOpenGLMaterialStateStage& St = MS.Stages[s];
			St.Texture				= Tex[s];
			St.TextureAddressU		= D3DTADDRESS_CLAMP;
			St.TextureAddressV		= D3DTADDRESS_CLAMP;
			St.TextureAddressW		= D3DTADDRESS_CLAMP;
			St.TextureMipLODBias	= 0.f;
			St.MinFilter			= D3DTEXF_POINT;
			St.MagFilter			= D3DTEXF_POINT;
			St.MipFilter			= D3DTEXF_NONE;
			St.TexCoordIndex		= s;
			St.TexCoordCount		= 0;
			St.TextureTransformsEnabled = 0;
			St.ResultArg			= D3DTA_CURRENT;
		}
	}

	void RunCase( const FCTCase& Case )
	{
		if( Case.NumStages > RenDev->NumTextureUnits )
		{
			Skip( Case.Name, *FString::Printf( TEXT("needs %i stages, this path has %i"), Case.NumStages, RenDev->NumTextureUnits ) );
			return;
		}
		FLOAT	Coords[4][MAX_STAGES][4];
		static const FLOAT UV[4][2] = { { 0.f, 0.f }, { 1.f, 0.f }, { 0.f, 1.f }, { 1.f, 1.f } };
		for( INT v=0; v<4; v++ )
			for( INT s=0; s<MAX_STAGES; s++ )
			{
				FLOAT Q = (s == 0 && (Case.Flags & CTF_Projected)) ? 2.f : 1.f;
				Coords[v][s][0] = UV[v][0] * Q; Coords[v][s][1] = UV[v][1] * Q; Coords[v][s][2] = 0.f; Coords[v][s][3] = Q;
			}
		FLOAT	MaxError = 0.f;
		INT		Compared = 0;
		FString	Worst;
		UBOOL	ProgramMissing = 0;
		for( INT Combo=0; Combo<3; Combo++ )
		{
			FOpenGLMaterialState MS;
			InitPass( MS, Case.NumStages, Factor[Combo] );
			for( INT s=0; s<Case.NumStages; s++ )
			{
				const FCTStage& Src = Case.Stages[s];
				FOpenGLMaterialStateStage& St = MS.Stages[s];
				St.ColorOp = (D3DTEXTUREOP)Src.ColorOp; St.ColorArg1 = Src.CA1; St.ColorArg2 = Src.CA2; St.ColorArg0 = Src.CA0;
				St.AlphaOp = (D3DTEXTUREOP)Src.AlphaOp; St.AlphaArg1 = Src.AA1; St.AlphaArg2 = Src.AA2; St.AlphaArg0 = Src.AA0;
			}
			BYTE Out[16*4];
			if( !Draw( MS, Diffuse[Combo], Specular[Combo], (Case.Flags & CTF_ColorSum) != 0, (Case.Flags & CTF_Fog) != 0, Coords, Out, (Case.Flags & CTF_FogFront) ? 0.5f : -0.5f ) )
				ProgramMissing = 1;
			FLOAT D[4], F[4], S[4];
			for( INT c=0; c<4; c++ )
			{
				D[c] = Diffuse[Combo][c] / 255.f;
				F[c] = Factor[Combo][c] / 255.f;
				S[c] = Specular[Combo][c] / 255.f;
			}
			for( INT p=0; p<16; p++ )
			{
				FLOAT Cur[4] = { D[0], D[1], D[2], D[3] };
				for( INT s=0; s<Case.NumStages; s++ )
				{
					FLOAT T[4];
					for( INT c=0; c<4; c++ )
						T[c] = TexData[s][p*4+c] / 255.f;
					CPUStage( Case.Stages[s], s, D, F, S, T, Cur );
				}
				if( Case.Flags & CTF_ColorSum )
					for( INT c=0; c<3; c++ )
						Cur[c] = CTSat( Cur[c] + S[c] );
				if( Case.Flags & CTF_Fog )
				{
					// Linear fog from 0 to 1 at eye distance 0.5: half the fog colour (1, 0.5, 0).
					static const FLOAT FogColor[3] = { 1.f, 0.5f, 0.f };
					for( INT c=0; c<3; c++ )
						Cur[c] = 0.5f * Cur[c] + 0.5f * FogColor[c];
				}
				for( INT c=0; c<4; c++ )
				{
					FLOAT Err = Abs( Out[p*4+c] - Cur[c] * 255.f );
					Compared++;
					if( Err > MaxError )
					{
						MaxError = Err;
						Worst = FString::Printf( TEXT("worst: colours %i, pixel %i, channel %i: %i, expected %.1f"), Combo, p, c, Out[p*4+c], Cur[c] * 255.f );
					}
				}
			}
		}
		if( ProgramMissing )
			Worst = FString::Printf( TEXT("NO PROGRAM (drawn with the texture environment); %s"), *Worst );
		Report( Case.Name, Compared, MaxError, 2.f, *Worst );
	}

	void RunCube()
	{
		// One draw per face: the coordinates of all four vertices point at that face.
		static const FLOAT Dir[6][3] = { { 1.f, .2f, .1f }, { -1.f, .1f, -.2f }, { .2f, 1.f, .1f }, { .1f, -1.f, .2f }, { -.2f, .1f, 1.f }, { .1f, .2f, -1.f } };
		FLOAT	MaxError = 0.f;
		INT		Compared = 0;
		FString	Worst;
		for( INT f=0; f<6; f++ )
		{
			FOpenGLMaterialState MS;
			InitPass( MS, 1, Factor[0] );
			MS.Stages[0].Texture = Cube;
			MS.Stages[0].ColorOp = D3DTOP_SELECTARG1; MS.Stages[0].ColorArg1 = D3DTA_TEXTURE;
			MS.Stages[0].AlphaOp = D3DTOP_SELECTARG1; MS.Stages[0].AlphaArg1 = D3DTA_TEXTURE;
			FLOAT Coords[4][MAX_STAGES][4];
			appMemzero( Coords, sizeof(Coords) );
			for( INT v=0; v<4; v++ )
			{
				Coords[v][0][0] = Dir[f][0]; Coords[v][0][1] = Dir[f][1]; Coords[v][0][2] = Dir[f][2]; Coords[v][0][3] = 1.f;
			}
			BYTE Out[16*4];
			Draw( MS, Diffuse[0], Specular[0], 0, 0, Coords, Out );
			BYTE Texel[4] = { (BYTE)(40 * f + 10), (BYTE)(250 - 37 * f), (BYTE)(f & 1 ? 200 : 60), (BYTE)(255 - 20 * f) };
			for( INT p=0; p<16; p++ )
				for( INT c=0; c<4; c++ )
				{
					FLOAT Err = Abs( (FLOAT)Out[p*4+c] - (FLOAT)Texel[c] );
					Compared++;
					if( Err > MaxError )
					{
						MaxError = Err;
						Worst = FString::Printf( TEXT("worst: face %i, channel %i: %i, expected %i"), f, c, Out[p*4+c], Texel[c] );
					}
				}
		}
		Report( TEXT("cube-faces"), Compared, MaxError, 0.f, *Worst );
	}

	void RunLODBias( INT Stage )
	{
		FString Name = FString::Printf( TEXT("lod-bias-stage%i"), Stage );
		if( Stage + 1 > RenDev->NumTextureUnits )
		{
			Skip( *Name, *FString::Printf( TEXT("needs %i stages, this path has %i"), Stage + 1, RenDev->NumTextureUnits ) );
			return;
		}
		// (F-3: on OpenGL ES, GL4ES' fixed-function shaders apply the unit's bias since our fork's
		// "Apply the texture unit's LOD bias" commit; before, this case was skipped there.)
		// Bias +1 on a texture drawn 1:1 (LOD 0) takes mip level 1 (green); -glnolodbias: level 0 (red).
		FOpenGLMaterialState MS;
		InitPass( MS, Stage + 1, Factor[0] );
		for( INT s=0; s<Stage; s++ )
		{
			MS.Stages[s].ColorOp = D3DTOP_SELECTARG1; MS.Stages[s].ColorArg1 = D3DTA_CURRENT;
			MS.Stages[s].AlphaOp = D3DTOP_SELECTARG1; MS.Stages[s].AlphaArg1 = D3DTA_CURRENT;
		}
		FOpenGLMaterialStateStage& St = MS.Stages[Stage];
		St.Texture = Mip;
		St.MipFilter = D3DTEXF_POINT;
		St.TextureMipLODBias = 1.f;
		St.ColorOp = D3DTOP_SELECTARG1; St.ColorArg1 = D3DTA_TEXTURE;
		St.AlphaOp = D3DTOP_SELECTARG1; St.AlphaArg1 = D3DTA_TEXTURE;
		FLOAT Coords[4][MAX_STAGES][4];
		static const FLOAT UV[4][2] = { { 0.f, 0.f }, { 1.f, 0.f }, { 0.f, 1.f }, { 1.f, 1.f } };
		for( INT v=0; v<4; v++ )
			for( INT s=0; s<MAX_STAGES; s++ )
			{
				Coords[v][s][0] = UV[v][0]; Coords[v][s][1] = UV[v][1]; Coords[v][s][2] = 0.f; Coords[v][s][3] = 1.f;
			}
		BYTE Out[16*4];
		Draw( MS, Diffuse[0], Specular[0], 0, 0, Coords, Out );
		BYTE	Expected[4] = { (BYTE)(RenDev->NoLODBias ? 255 : 0), (BYTE)(RenDev->NoLODBias ? 0 : 255), 0, 255 };
		FLOAT	MaxError = 0.f;
		for( INT p=0; p<16; p++ )
			for( INT c=0; c<4; c++ )
				MaxError = Max( MaxError, Abs( (FLOAT)Out[p*4+c] - (FLOAT)Expected[c] ) );
		Report( *Name, 64, MaxError, 0.f, *FString::Printf( TEXT("stage %i biased +1 reads mip level %s (pixel 0: %i,%i,%i; level 0 is red, 1 green, 2 blue)"),
			Stage, RenDev->NoLODBias ? TEXT("0 (-glnolodbias)") : TEXT("1"), Out[0], Out[1], Out[2] ) );
	}

	void Run()
	{
		Passed = Failed = Skipped = 0;
		const TCHAR* Path = RenDev->FFProgramsActive ? TEXT("fragment programs") :
							RenDev->IsGLES ? TEXT("texture environment through GL4ES") :
							RenDev->HasCombine3() ? TEXT("texture environment with combine3") :
							RenDev->HasCombine4() ? TEXT("texture environment with combine4") : TEXT("texture environment without combine3 or combine4");
		debugf( NAME_Init, TEXT("UC2GLDrv: combinertest of the texture stages (frame %i): %s, GL %s"),
			RenDev->FrameCounter, Path, appFromAnsi((ANSICHAR*)RenDev->glGetString(GL_RENDERER)) );
		// What the path can do, not what the material compiler is told (MaterialStages, 4):
		// the test draws its own passes.
		INT CompilerStages = RenDev->NumTextureUnits;
		RenDev->NumTextureUnits = Min<INT>( MAX_STAGES, Min<INT>( RenDev->MaxTextureUnits, RenDev->FFProgramsActive ? RenDev->GLProgramUnits : RenDev->GLFixedFunctionUnits ) );
		RI->PushState();
		Setup();
		for( INT i=0; i<ARRAY_COUNT(GCTCases); i++ )
			RunCase( GCTCases[i] );
		RunCube();
		RunLODBias( 0 );
		RunLODBias( 5 );
		Teardown();
		RI->PopState();
		INT PathStages = RenDev->NumTextureUnits;
		RenDev->NumTextureUnits = CompilerStages;
		// The engine's state comes back with the next pass; fog as the scene had it.
		if( RI->CurrentState->DistanceFogEnabled )
		{
			FPlane FogColor = RI->CurrentState->DistanceFogColor.Plane();
			RenDev->glEnable( GL_FOG );
			RenDev->glFogf( GL_FOG_START, RI->CurrentState->DistanceFogStart );
			RenDev->glFogf( GL_FOG_END, RI->CurrentState->DistanceFogEnd );
			RenDev->glFogfv( GL_FOG_COLOR, (GLfloat*)&FogColor );
		}
		RenDev->glEnable( GL_SCISSOR_TEST );
		RI->LightsDirty = RI->MatricesDirty = 1;
		RI->RestoreAfterDeviceDraw();
		debugf( NAME_Init, TEXT("UC2GLDrv: combinertest summary: %i passed, %i failed, %i skipped (%s, %i stages on this path, %i given to the material compiler)"), Passed, Failed, Skipped, Path, PathStages, CompilerStages );
	}
};

void UC2GL_RunCombinerTest( UUC2GLRenderDevice* RenDev, UViewport* Viewport )
{
	FUC2GLCombinerTest Test;
	Test.RenDev	= RenDev;
	Test.RI		= &RenDev->RenderInterface;
	Test.Run();
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
