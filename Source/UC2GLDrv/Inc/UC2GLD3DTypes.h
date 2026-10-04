/*=============================================================================
	UC2GLD3DTypes.h: the Direct3D 8 fixed-function vocabulary the material compiler
	writes in.

	UC2GLDrv's material compiler (UC2GLMaterialState.cpp) is UC2's D3DDrv
	D3DMaterialState.cpp carried over function for function: it describes every pass as
	D3D8 texture-stage state (D3DTOP_*, D3DTA_*, D3DTSS_TCI_*, D3DTTFF_*) and D3D8 blend
	state (D3DBLEND_*). Keeping that vocabulary keeps the file diffable against the
	specification, so a fix there ports here line for line. The translation to OpenGL
	happens once, when a pass is committed (FOpenGLRenderInterface::SetMaterialBlending and
	CommitTexCoords).

	Values are those of d3d8types.h (they only need to be distinct here, but matching them
	keeps debugger output readable against D3D tools). No Direct3D header is included.
=============================================================================*/

#ifndef _INC_UC2GLD3DTYPES
#define _INC_UC2GLD3DTYPES

// Texture stage operations (D3DTEXTUREOP).
typedef DWORD D3DTEXTUREOP;
enum
{
	D3DTOP_DISABLE					= 1,
	D3DTOP_SELECTARG1				= 2,
	D3DTOP_SELECTARG2				= 3,
	D3DTOP_MODULATE					= 4,
	D3DTOP_MODULATE2X				= 5,
	D3DTOP_MODULATE4X				= 6,
	D3DTOP_ADD						= 7,
	D3DTOP_ADDSIGNED				= 8,
	D3DTOP_ADDSIGNED2X				= 9,
	D3DTOP_SUBTRACT					= 10,
	D3DTOP_ADDSMOOTH				= 11,
	D3DTOP_BLENDDIFFUSEALPHA		= 12,
	D3DTOP_BLENDTEXTUREALPHA		= 13,
	D3DTOP_BLENDFACTORALPHA			= 14,
	D3DTOP_BLENDTEXTUREALPHAPM		= 15,
	D3DTOP_BLENDCURRENTALPHA		= 16,
	D3DTOP_PREMODULATE				= 17,
	D3DTOP_MODULATEALPHA_ADDCOLOR	= 18,
	D3DTOP_MODULATECOLOR_ADDALPHA	= 19,
	D3DTOP_MODULATEINVALPHA_ADDCOLOR= 20,
	D3DTOP_MODULATEINVCOLOR_ADDALPHA= 21,
	D3DTOP_BUMPENVMAP				= 22,
	D3DTOP_BUMPENVMAPLUMINANCE		= 23,
	D3DTOP_DOTPRODUCT3				= 24,
	D3DTOP_MULTIPLYADD				= 25,
	D3DTOP_LERP						= 26,
};

// Texture stage arguments (D3DTA_*).
enum
{
	D3DTA_SELECTMASK		= 0x0000000f,
	D3DTA_DIFFUSE			= 0x00000000,
	D3DTA_CURRENT			= 0x00000001,
	D3DTA_TEXTURE			= 0x00000002,
	D3DTA_TFACTOR			= 0x00000003,
	D3DTA_SPECULAR			= 0x00000004,
	D3DTA_TEMP				= 0x00000005,
	D3DTA_COMPLEMENT		= 0x00000010,
	D3DTA_ALPHAREPLICATE	= 0x00000020,
};

// Frame buffer blend factors (D3DBLEND) and operations (D3DBLENDOP).
typedef DWORD D3DBLEND;
enum
{
	D3DBLEND_ZERO				= 1,
	D3DBLEND_ONE				= 2,
	D3DBLEND_SRCCOLOR			= 3,
	D3DBLEND_INVSRCCOLOR		= 4,
	D3DBLEND_SRCALPHA			= 5,
	D3DBLEND_INVSRCALPHA		= 6,
	D3DBLEND_DESTALPHA			= 7,
	D3DBLEND_INVDESTALPHA		= 8,
	D3DBLEND_DESTCOLOR			= 9,
	D3DBLEND_INVDESTCOLOR		= 10,
	D3DBLEND_SRCALPHASAT		= 11,
};
typedef DWORD D3DBLENDOP;
enum
{
	D3DBLENDOP_ADD			= 1,
	D3DBLENDOP_SUBTRACT		= 2,
	D3DBLENDOP_REVSUBTRACT	= 3,
	D3DBLENDOP_MIN			= 4,
	D3DBLENDOP_MAX			= 5,
};

// Texture addressing (D3DTEXTUREADDRESS) and filtering (D3DTEXTUREFILTERTYPE).
typedef DWORD D3DTEXTUREADDRESS;
enum
{
	D3DTADDRESS_WRAP		= 1,
	D3DTADDRESS_MIRROR		= 2,
	D3DTADDRESS_CLAMP		= 3,
	D3DTADDRESS_BORDER		= 4,
	D3DTADDRESS_MIRRORONCE	= 5,
};
enum
{
	D3DTEXF_NONE			= 0,
	D3DTEXF_POINT			= 1,
	D3DTEXF_LINEAR			= 2,
	D3DTEXF_ANISOTROPIC		= 3,
};

// Texture coordinate index flags (D3DTSS_TEXCOORDINDEX) and transform flags (D3DTTFF).
enum
{
	D3DTSS_TCI_PASSTHRU						= 0x00000000,
	D3DTSS_TCI_CAMERASPACENORMAL			= 0x00010000,
	D3DTSS_TCI_CAMERASPACEPOSITION			= 0x00020000,
	D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR	= 0x00030000,
};
enum
{
	D3DTTFF_DISABLE		= 0,
	D3DTTFF_COUNT1		= 1,
	D3DTTFF_COUNT2		= 2,
	D3DTTFF_COUNT3		= 3,
	D3DTTFF_COUNT4		= 4,
	D3DTTFF_PROJECTED	= 256,
};

// Cull modes (D3DCULL).
typedef DWORD D3DCULL;
enum
{
	D3DCULL_NONE	= 1,
	D3DCULL_CW		= 2,
	D3DCULL_CCW		= 3,
};

// Light types (D3DLIGHTTYPE): only used to count lights the way FD3DRenderInterface does.
typedef INT D3DLIGHTTYPE;
enum
{
	D3DLIGHT_POINT			= 1,
	D3DLIGHT_SPOT			= 2,
	D3DLIGHT_DIRECTIONAL	= 3,
};

typedef DWORD D3DCOLOR;

// A D3D matrix: FMatrix's layout (row-major, row vectors; _RC == M[R-1][C-1]).
struct D3DMATRIX
{
	union
	{
		struct
		{
			FLOAT	_11, _12, _13, _14,
					_21, _22, _23, _24,
					_31, _32, _33, _34,
					_41, _42, _43, _44;
		};
		FLOAT m[4][4];
	};

	operator FMatrix() const { return *(const FMatrix*)this; }
	D3DMATRIX& operator=( const FMatrix& Other ) { appMemcpy( this, &Other, sizeof(FMatrix) ); return *this; }
};

// The largest value of a 24-bit depth buffer (d3d8types.h, Xbox).
#ifndef D3DZ_MAX_D24S8
#define D3DZ_MAX_D24S8 0x00ffffff
#endif

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
