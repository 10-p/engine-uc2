/*=============================================================================
	UC2GLD3DXCompat.h: the handful of D3DX matrix helpers UC2's material compiler calls,
	implemented on FMatrix (private to UC2GLMaterialState.cpp).

	D3DMATRIX/D3DXMATRIX have FMatrix's layout (row-major, row vectors: _RC == M[R-1][C-1])
	and D3DX's products are FMatrix's (Out = A*B applies A first), so these are exact.
=============================================================================*/

#ifndef _INC_UC2GLD3DXCOMPAT
#define _INC_UC2GLD3DXCOMPAT

typedef D3DMATRIX D3DXMATRIX;

FORCEINLINE D3DXMATRIX* D3DXMatrixMultiply( D3DXMATRIX* Out, const D3DXMATRIX* A, const D3DXMATRIX* B )
{
	FMatrix Result = (*(const FMatrix*)A) * (*(const FMatrix*)B);
	*(FMatrix*)Out = Result;
	return Out;
}

FORCEINLINE D3DXMATRIX* D3DXMatrixInverse( D3DXMATRIX* Out, FLOAT* Determinant, const D3DXMATRIX* M )
{
	FMatrix Source = *(const FMatrix*)M;
	FMatrix Result = Source.Inverse();
	if( Determinant )
		*Determinant = Source.Determinant();
	*(FMatrix*)Out = Result;
	return Out;
}

FORCEINLINE D3DXMATRIX* D3DXMatrixTranspose( D3DXMATRIX* Out, const D3DXMATRIX* M )
{
	FMatrix Source = *(const FMatrix*)M;
	FMatrix Result = Source.Transpose();
	*(FMatrix*)Out = Result;
	return Out;
}

FORCEINLINE D3DXMATRIX* D3DXMatrixIdentity( D3DXMATRIX* Out )
{
	*(FMatrix*)Out = FMatrix::Identity;
	return Out;
}

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
