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

// The vector helpers of SetProjectorMultiMaterial (Epic's Xbox body). D3DX's
// TransformNormal is v * M (upper 3x3), Transform is (v,1) * M; the same as FMatrix's.
struct D3DXVECTOR3
{
	FLOAT x, y, z;
};
struct D3DXVECTOR4
{
	FLOAT x, y, z, w;
	D3DXVECTOR4() {}
	D3DXVECTOR4( FLOAT X, FLOAT Y, FLOAT Z, FLOAT W ) : x(X), y(Y), z(Z), w(W) {}
};

FORCEINLINE D3DXVECTOR3* D3DXVec3TransformNormal( D3DXVECTOR3* Out, const D3DXVECTOR3* V, const D3DXMATRIX* M )
{
	FVector R = ((const FMatrix*)M)->TransformNormal( FVector( V->x, V->y, V->z ) );
	Out->x = R.X; Out->y = R.Y; Out->z = R.Z;
	return Out;
}

FORCEINLINE D3DXVECTOR4* D3DXVec3Transform( D3DXVECTOR4* Out, const D3DXVECTOR3* V, const D3DXMATRIX* M )
{
	FPlane R = ((const FMatrix*)M)->TransformFPlane( FPlane( V->x, V->y, V->z, 1.f ) );
	Out->x = R.X; Out->y = R.Y; Out->z = R.Z; Out->w = R.W;
	return Out;
}

FORCEINLINE D3DXVECTOR3* D3DXVec3Normalize( D3DXVECTOR3* Out, const D3DXVECTOR3* V )
{
	FVector R = FVector( V->x, V->y, V->z ).SafeNormal();
	Out->x = R.X; Out->y = R.Y; Out->z = R.Z;
	return Out;
}

FORCEINLINE D3DXVECTOR3* D3DXVec3Subtract( D3DXVECTOR3* Out, const D3DXVECTOR3* A, const D3DXVECTOR3* B )
{
	Out->x = A->x - B->x; Out->y = A->y - B->y; Out->z = A->z - B->z;
	return Out;
}

FORCEINLINE FLOAT D3DXVec3Length( const D3DXVECTOR3* V )
{
	return appSqrt( V->x * V->x + V->y * V->y + V->z * V->z );
}

FORCEINLINE FLOAT D3DXVec3Dot( const D3DXVECTOR3* A, const D3DXVECTOR3* B )
{
	return A->x * B->x + A->y * B->y + A->z * B->z;
}

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
