/* =============================================================================
   UnColor.h: Color; promoted from UnRenderResource.h so Core functions can 
   manipulate colors.

   Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

   Revision history:
     * Created by Jack Porter
	 * promoted by scion bcl
  ============================================================================= 
*/

#ifndef _UNCOLOR_H_
#define _UNCOLOR_H_

/*-----------------------------------------------------------------------------
  FColor.
-----------------------------------------------------------------------------*/

class CORE_API FColor
{
public:
	// Variables.
#if __INTEL_BYTE_ORDER__
#ifdef __PSX2_EE__
	BYTE R GCC_ALIGN(4);
	BYTE G,B,A;
#else
#if _MSC_VER
	union { struct{ BYTE B,G,R,A; }; DWORD AlignmentDummy; };
#else
	BYTE B GCC_ALIGN(4);
	BYTE G,R,A;
#endif
#endif
#else
	//!!TODO: handle packing.
	BYTE A,R,G,B;
#endif

	DWORD& DWColor(void) {return *((DWORD*)this);}
	const DWORD& DWColor(void) const {return *((DWORD*)this);}

	// Constructors.
	FColor() {}
	FColor( BYTE InR, BYTE InG, BYTE InB, BYTE InA = 255 )
		:	R(InR), G(InG), B(InB), A(InA) {}
	FColor( const FPlane& P )
		:	R(Clamp(appFloor(P.X*255),0,255))
		,	G(Clamp(appFloor(P.Y*255),0,255))
		,	B(Clamp(appFloor(P.Z*255),0,255))
		,	A(Clamp(appFloor(P.W*255),0,255))
		{}
	FColor( DWORD InColor )
		{ DWColor() = InColor; }

		// Serializer.
	friend FArchive& operator<< (FArchive &Ar, FColor &Color )
	{
		return Ar << Color.R << Color.G << Color.B << Color.A;
	}

		// Operators.
	UBOOL operator==( const FColor &C ) const
	{
		return DWColor() == C.DWColor();
	}
	UBOOL operator!=( const FColor& C ) const
	{
		return DWColor() != C.DWColor();
	}
	void operator+=(FColor C)
	{
#ifdef ASM
			_asm
			{
				mov edi, DWORD PTR [this]
				mov ebx, DWORD PTR [C]
				mov eax, DWORD PTR [edi]

				xor ecx, ecx
					add al, bl
					adc ecx, 0xffffffff
					not ecx
					or al, cl
					mov BYTE PTR [edi], al
					inc edi
					shr eax, 8
					shr ebx, 8

					xor ecx, ecx
					add al, bl
					adc ecx, 0xffffffff
					not ecx
					or al, cl
					mov BYTE PTR [edi], al
					inc edi
					shr eax, 8
					shr ebx, 8

					xor ecx, ecx
					add al, bl
					adc ecx, 0xffffffff
					not ecx
					or al, cl
					mov BYTE PTR [edi], al
					inc edi
					shr eax, 8
					shr ebx, 8

					xor ecx, ecx
					add al, bl
					adc ecx, 0xffffffff
					not ecx
					or al, cl
					mov BYTE PTR [edi], al
			}
#else
		R = (BYTE) Min((INT) R + (INT) C.R,255);
		G = (BYTE) Min((INT) G + (INT) C.G,255);
		B = (BYTE) Min((INT) B + (INT) C.B,255);
		A = (BYTE) Min((INT) A + (INT) C.A,255);
#endif
	}
	INT Brightness() const
	{
		return (2*(INT)R + 3*(INT)G + 1*(INT)B)>>3;
	}
	FLOAT FBrightness() const
	{
		return (2.f*R + 3.f*G + 1.f*B)/(6.f*256.f);
	}
	DWORD TrueColor() const
	{
		return DWColor();
	}
	_WORD HiColor565() const
	{
		return ((R >> 3) << 11) + ((G >> 2) << 5) + (B >> 3);
	}
	_WORD HiColor555() const
	{
		return ((R >> 3) << 10) + ((G >> 3) << 5) + (B >> 3);
	}
	_WORD HiColor4444() const
	{
		return ((A >> 4) << 12) + ((R >> 4) << 8) + ((G >> 4) << 4) + (B >> 4);
	}
	FPlane Plane() const
	{
		//scion jg -- Multiplying by precalced 1/255 is faster
		return FPlane(R * ONE_OVER_255,G * ONE_OVER_255,B * ONE_OVER_255,A * ONE_OVER_255);
	}
	FColor Brighten( INT Amount )
	{
		return FColor( Plane() * (1.f - Amount/24.f) );
	}
	FColor RenderColor()
	{
#ifndef _XBOX //scion jg -- Not needed on the xbox
		if( GIsOpenGL )
			return FColor( B, G, R, A);
		else
#endif
			return *this;
	}
	operator FPlane() const
	{
		//scion jg -- Multiplying by precalced 1/255 is faster
		return FPlane(R * ONE_OVER_255,G * ONE_OVER_255,B * ONE_OVER_255,A * ONE_OVER_255);
	}
	operator FVector() const
	{
		//scion jg -- Multiplying by precalced 1/255 is faster
		return FVector(R * ONE_OVER_255, G * ONE_OVER_255, B * ONE_OVER_255);
	}
	operator DWORD() const 
	{ 
		return DWColor(); 
	}
	//scion jg -- Added color lerp support if flAmount is 0 it is all this
	// color. If it is 1, it is all the Other color
	void Lerp(const FColor& Other,FLOAT flAmount)
	{
		FLOAT fl = Clamp<FLOAT>(flAmount,0.f,1.f);
		R = appTrunc((FLOAT)R + fl * (Other.R - R));
		G = appTrunc((FLOAT)G + fl * (Other.G - G));
		B = appTrunc((FLOAT)B + fl * (Other.B - B));
		A = appTrunc((FLOAT)A + fl * (Other.A - A));
	}
	//scion jg -- Added color lerp support if flAmount is 0 it is all Min
	// color. If it is 1, it is all the Other color
	void Lerp(const FColor& Min,const FColor& Max,FLOAT flAmount)
	{
		FLOAT fl = Clamp<FLOAT>(flAmount,0.f,1.f);
		R = appTrunc((FLOAT)Min.R + fl * (Max.R - Min.R));
		G = appTrunc((FLOAT)Min.G + fl * (Max.G - Min.G));
		B = appTrunc((FLOAT)Min.B + fl * (Max.B - Min.B));
		A = appTrunc((FLOAT)Min.A + fl * (Max.A - Min.A));
	}

	// scion sz - return HSV of current RGB color
	// HSV = Hue.Saturation.Brightness
	static FVector RGBtoHSV(FVector RGB)
	{
		guard(FHeaderItem::ToHSL);
		FVector HSL(0,0,0);
		float minval = Min3(RGB.X, RGB.Y, RGB.Z);
		float maxval = Max3(RGB.X, RGB.Y, RGB.Z);
		float mdiff  = float(maxval) - float(minval);
		float msum   = float(maxval) + float(minval);

		HSL.Z = msum / 2.0f;

		if (maxval == minval) 
		{
			HSL.Y = 0.0f;
			HSL.X = 0.0f; 
		}   
		else 
		{ 
			float rnorm = (maxval - RGB.X  ) / mdiff;      
			float gnorm = (maxval - RGB.Y) / mdiff;
			float bnorm = (maxval - RGB.Z ) / mdiff;   
			HSL.Y = (HSL.Z <= 0.5f) ? (mdiff / msum) : (mdiff / (510.0f - msum));

			if (RGB.X   == maxval) HSL.X = 60.0f * (6.0f + bnorm - gnorm);
			if (RGB.Y == maxval) HSL.X = 60.0f * (2.0f + rnorm - bnorm);
			if (RGB.Z  == maxval) HSL.X = 60.0f * (4.0f + gnorm - rnorm);
			if (HSL.X > 360.0f) HSL.X = HSL.X - 360.0f;
		}

		HSL.X = 255.0f * (HSL.X / 360.0f);
		HSL.Y *= 255.0f;
		HSL.Z *= 255.0f;

		return HSL;
		unguard;
	}

	// scion sz - 
	static FVector HSVtoRGB(FVector HSV)
	{
		FLOAT Brightness = HSV.Z * 1.4f / 255.f;
		Brightness *= 0.7f/(0.01f + appSqrt(Brightness));
		Brightness  = Clamp(Brightness,0.f,1.f);
		FVector Hue = (HSV.X<86) ? FVector((85-HSV.X)/85.f,(HSV.X-0)/85.f,0) : (HSV.X<171) ? FVector(0,(170-HSV.X)/85.f,(HSV.X-85)/85.f) : FVector((HSV.X-170)/85.f,0,(255-HSV.X)/84.f);
		return FVector( (Hue + HSV.Y/255.f * (FVector(1,1,1) - Hue)) * Brightness );
	}
}; // FColor
#endif // _UNCOLOR_H_