

//
// Forward declarations
//
class	UMaterial;
class		URenderedMaterial;
class			UBitmapMaterial;
class				UTexture;

class	UModifier;

class FTexture;
class FCubemap;

//
// EFillMode
//
enum EFillMode
{
	FM_Wireframe	= 0,
	FM_FlatShaded	= 1,
	FM_Solid		= 2,
};

/*-----------------------------------------------------------------------------
	UMaterial.
-----------------------------------------------------------------------------*/

//scion superville: Material specific debris types
enum EMaterialType
{
	MT_None				= 0,
	MT_Flesh_Nakhti		= 1,
	MT_Flesh_Skaarj		= 2,
	MT_Flesh_Liandri	= 3,
	MT_Flesh_Necris		= 4,
	MT_Flesh_Jugg		= 5,
	MT_Flesh_Izarian	= 6,
	MT_Flesh_Nali		= 7,
	MT_Dirt				= 8,
	MT_Stone			= 9,
	MT_Metal			= 10,
	MT_Wood				= 11,
	MT_Snow				= 12,
	MT_Water			= 13,
	MT_Lava				= 14,
	MT_Electric			= 15,
	MT_Energy			= 16,
	MT_Explosive		= 17,
	MT_Glass			= 18,
	MT_Cloth			= 19,
	MT_Tile				= 20,
	MT_Gravel			= 21,
	MT_Plastic			= 22,
	MT_Paper			= 23,
	MT_Slime			= 24,
	MT_Foliage			= 25,
	MT_Plaster			= 26,
	MT_Mud				= 27,
	MT_Sand				= 28,
	MT_ShallowWater		= 29,
	MT_Max				= 30
};

//scion jg -- Enum for fast material determination
enum EMaterialRenderHint
{
	MRH_Material			=	(1 << 0),
	MRH_Modifier			=	(1 << 1),
	MRH_Combiner			=	(1 << 2),
	MRH_Shader				=	(1 << 3),
	MRH_Texture				=	(1 << 4),
	MRH_ColorModifier		=	(1 << 5),
	MRH_FinalBlend			=	(1 << 6),
	MRH_OpacityModifier		=	(1 << 7),
	MRH_BitmapMaterial		=	(1 << 8),
	MRH_ProjectorMaterial	=	(1 << 9),
	MRH_TerrainMaterial		=	(1 << 10),
	MRH_ParticleMaterial	=	(1 << 11),
	MRH_ConstantMaterial	=	(1 << 12),
	MRH_VertexColor			=	(1 << 13),
	MRH_TexModifier			=	(1 << 14),
	//scion specific materials
	MRH_PixelShaderMaterial	=	(1 << 16),
	MRH_MultiPassMaterial	=	(1 << 17),
	MRH_MovieMaterial		=	(1 << 18),
	MRH_DistortionBaseMaterial = (1 << 19),
	MRH_BlendedDistortionMaterial = (1 << 20),
	MRH_SkinShader =			(1 << 21),
	MRH_TeamEvironShader =		(1 << 22),
	MRH_ProjectorMultiMaterial =(1 << 23)
};

// scion sz -
enum EPostProcessMaterialType
{
	POSTPROCESS_NONE,
	POSTPROCESS_NOBLEND,
	POSTPROCESS_BLEND,
	POSTPROCESS_BACKBUFFER
};

class ENGINE_API UMaterial : public UObject
{
	//scion rwd - added auto-precache
	DECLARE_ABSTRACT_CLASS(UMaterial,UObject,CLASS_AutoPrecache|CLASS_FullDefaults,Engine)
	
	//scion superville
	BYTE MaterialType GCC_PACK(4);

	UMaterial*	DefaultMaterial;
    BITFIELD	Validated:1 		GCC_PACK(4);

	//scion jg -- This is the bitfield enumeration for fast material checks
	INT			RenderHint;

	// Constructor.
	UMaterial();

	// UObject interface
	void Serialize( FArchive& Ar );

	// UMaterial interface
	virtual UBOOL CheckCircularReferences( TArray<UMaterial*>& History );
	virtual UBOOL GetValidated() { return Validated; }
	virtual void SetValidated( UBOOL InValidated ) { Validated = InValidated; }
	virtual void PreSetMaterial(FLOAT TimeSeconds) {};

	// Getting information about a combined material:
	virtual INT MaterialUSize() { return 0; }
	virtual INT MaterialVSize() { return 0; }
	virtual UBOOL RequiresSorting() { return 0; }
	virtual UBOOL IsTransparent() { return 0; }
	virtual BYTE RequiredUVStreams() { return 1; }
	virtual UBOOL RequiresNormal() { return 0; }
	virtual EPostProcessMaterialType IsPostProcess() { return POSTPROCESS_NONE; }
	//scion jg -- Whether a material is emissive or not
	virtual UBOOL IsEmissive(void) { return 0; }

	//!! OLDVER
	UMaterial* ConvertPolyFlagsToMaterial( UMaterial* InMaterial, DWORD InPolyFlags );

	DECLARE_FUNCTION(execMaterialUSize)
	DECLARE_FUNCTION(execMaterialVSize)

	//scion rwd - custom precache for materials
	void Precache(UObject *src = NULL,TMap<UObject*,BYTE> *masterPrecacheList = NULL);
};

/*-----------------------------------------------------------------------------
	URenderedMaterial.
-----------------------------------------------------------------------------*/

class ENGINE_API URenderedMaterial : public UMaterial
{
	DECLARE_ABSTRACT_CLASS(URenderedMaterial,UMaterial,0,Engine)
};

/*-----------------------------------------------------------------------------
	UBitmapMaterial.
-----------------------------------------------------------------------------*/
class ENGINE_API UBitmapMaterial : public URenderedMaterial
{
	DECLARE_ABSTRACT_CLASS(UBitmapMaterial,URenderedMaterial,0,Engine)

	BYTE		Format;				// ETextureFormat.
	BYTE		UClampMode;			// Texture U clamp mode
	BYTE		VClampMode;			// Texture V clamp mode
	BYTE		FilterMode;			// scion sz - filter mode (ETexFilterMode)

	BYTE		UBits, VBits;		// # of bits in USize, i.e. 8 for 256.
	INT			USize, VSize;		// Size, must be power of 2.
	INT			UClamp, VClamp;		// Clamped width, must be <= size.
	


	// UBitmapMaterial interface.
	virtual FBaseTexture* GetRenderInterface() = 0;
	virtual UBitmapMaterial* Get( FTime Time, UViewport* Viewport ) { return this; }

	// UMaterial Interface
	virtual INT MaterialUSize() { return USize; }
	virtual INT MaterialVSize() { return VSize; }
};

/*-----------------------------------------------------------------------------
	UProxyBitmapMaterial
-----------------------------------------------------------------------------*/
class ENGINE_API UProxyBitmapMaterial : public UBitmapMaterial
{
	DECLARE_CLASS(UProxyBitmapMaterial,UBitmapMaterial,0,Engine);

private:

	FBaseTexture*	TextureInterface;

public:

	// UProxyBitmapMaterial interface.
	FORCEINLINE void SetTextureInterface(FBaseTexture* InTextureInterface)
	{
		TextureInterface = InTextureInterface;
		Format = TextureInterface->GetFormat();
		UClampMode = TextureInterface->GetUClamp();
		VClampMode = TextureInterface->GetVClamp();
		UClamp = USize = TextureInterface->GetWidth();
		VClamp = VSize = TextureInterface->GetHeight();
		UBits = appCeilLogTwo(UClamp);
		VBits = appCeilLogTwo(VClamp);
		// scion sz - default
		FilterMode = TF_BILINEAR;
	}

	// UBitmapMaterial interface.
	virtual FBaseTexture* GetRenderInterface() { return TextureInterface; }
	virtual UBitmapMaterial* Get( FTime Time, UViewport* Viewport ) { return this; }
};

/*-----------------------------------------------------------------------------
	UTexCoordMaterial
-----------------------------------------------------------------------------*/
class ENGINE_API UTexCoordMaterial : public URenderedMaterial
{
    DECLARE_CLASS(UTexCoordMaterial,URenderedMaterial,0,Engine)

	class UBitmapMaterial* Texture;
    class UTexCoordGen* TextureCoords;

	// UMaterial interface
	virtual INT MaterialUSize() { return Texture ? Texture->MaterialUSize() : 0; }
	virtual INT MaterialVSize() { return Texture ? Texture->MaterialVSize() : 0; }
};

//scion jg -- Faster material type checking. It uses bit mask comparisons
// rather than directly using Cast<>(). If the material does not have a
// valid render hint set, it will use Cast<>() to resolve it. If you can
// guarantee that each material type will have a bitmask set, you can
// remove the fallback for improved performance.
template<class TYPE,DWORD RenderHint>
FORCEINLINE TYPE* MaterialCast(UMaterial* pMaterial)
{
	if (pMaterial == NULL) return NULL;
	// First check the material render hint for a match. If that fails,
	// check to see if this is an unregistered type. If it is, use the
	// fallback Cast<>() method to determine type. If not just fail
	return pMaterial->RenderHint & RenderHint ? (TYPE*)pMaterial :
		pMaterial->RenderHint == 0 ? Cast<TYPE>(pMaterial) : NULL;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/


