/*=============================================================================
	UnTemplate.h: Unreal templates.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	Type information.
-----------------------------------------------------------------------------*/

// scion sz - changed from float to 1/int
//#define MOVETOTOP_PCT	0.2f
#define MOVETOTOP_DIVISOR 4

#include <string.h>

//
// Type information for initialization.
//
template <class T> struct TTypeInfoBase
{
public:
	typedef const T& ConstInitType;
	static UBOOL NeedsDestructor() {return 1;}
	static UBOOL DefinitelyNeedsDestructor() {return 0;}
	static const T& ToInit( const T& In ) {return In;}
};
template <class T> struct TTypeInfo : public TTypeInfoBase<T>
{
};

template <> struct TTypeInfo<BYTE> : public TTypeInfoBase<BYTE>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<SBYTE> : public TTypeInfoBase<SBYTE>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<ANSICHAR> : public TTypeInfoBase<ANSICHAR>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<INT> : public TTypeInfoBase<INT>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<DWORD> : public TTypeInfoBase<DWORD>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<_WORD> : public TTypeInfoBase<_WORD>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<SWORD> : public TTypeInfoBase<SWORD>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<QWORD> : public TTypeInfoBase<QWORD>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<SQWORD> : public TTypeInfoBase<SQWORD>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<FName> : public TTypeInfoBase<FName>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};
template <> struct TTypeInfo<UObject*> : public TTypeInfoBase<UObject*>
{
public:
	static UBOOL NeedsDestructor() {return 0;}
};

/*-----------------------------------------------------------------------------
	Standard templates.
-----------------------------------------------------------------------------*/

template< class T > inline T Abs( const T A )
{
	return (A>=(T)0) ? A : -A;
}

#ifdef _XBOX
//scion jg -- This can be done in one instruction so use that instead
template< FLOAT > inline FLOAT Abs( const FLOAT A )
{
	return fabs(A);
}
#endif

template< class T > inline T Sgn( const T A )
{
	return (A>0) ? 1 : ((A<0) ? -1 : 0);
}
template< class T > inline T Max( const T A, const T B )
{
	return (A>=B) ? A : B;
}
template< class T > inline T Min( const T A, const T B )
{
	return (A<=B) ? A : B;
}
template< class T > inline T Max3( const T A, const T B, const T C )
{
	return Max ( Max( A, B ), C );
}
template< class T > inline T Min3( const T A, const T B, const T C )
{
	return Min ( Min( A, B ), C );
}
template< class T > inline T Square( const T A )
{
	return A*A;
}
template< class T > inline T Clamp( const T X, const T Min, const T Max )
{
	return X<Min ? Min : X<Max ? X : Max;
}
template< class T > inline T Align( const T Ptr, INT Alignment )
{
	return (T)(((DWORD)Ptr + Alignment - 1) & ~(Alignment-1));
}
template< class T > inline void Exchange( T& A, T& B )
{
	const T Temp = A;
	A = B;
	B = Temp;
}
template< class T > T Lerp( T& A, T& B, FLOAT Alpha )
{
	return (T)(A + Alpha * (B-A));
}
inline DWORD GetTypeHash( const BYTE A )
{
	return A;
}
inline DWORD GetTypeHash( const SBYTE A )
{
	return A;
}
inline DWORD GetTypeHash( const _WORD A )
{
	return A;
}
inline DWORD GetTypeHash( const SWORD A )
{
	return A;
}
inline DWORD GetTypeHash( const INT A )
{
	return A;
}
inline DWORD GetTypeHash( const DWORD A )
{
	return A;
}
inline DWORD GetTypeHash( const QWORD A )
{
	return (DWORD)A+((DWORD)(A>>32) * 23);
}
inline DWORD GetTypeHash( const SQWORD A )
{
	return (DWORD)A+((DWORD)(A>>32) * 23);
}
inline DWORD GetTypeHash( const TCHAR* S )
{
	return appStrihash(S);
}
#define ExchangeB(A,B) {UBOOL T=A; A=B; B=T;}

/*----------------------------------------------------------------------------
	Standard macros.
----------------------------------------------------------------------------*/

// Number of elements in an array.
#define ARRAY_COUNT( array ) \
	( sizeof(array) / sizeof((array)[0]) )

// Offset of a struct member.
#define STRUCT_OFFSET( struc, member ) \
	( (INT)&((struc*)NULL)->member )

/*-----------------------------------------------------------------------------
	Allocators.
-----------------------------------------------------------------------------*/

template <class T> class TAllocator
{};

/*-----------------------------------------------------------------------------
	Dynamic array template.
-----------------------------------------------------------------------------*/

//
// Base dynamic array.
//
//scion jg -- FORCEINLINE'd all small functions
class CORE_API FArray
{
public:
	FORCEINLINE void* GetData()
	{
		return Data;
	}
	FORCEINLINE const void* GetData() const
	{
		return Data;
	}
	// scion sz - sets data pointer in array directly
	// add,insert,remove are all unsafe
	FORCEINLINE void SetData( void* InData, DWORD Size )
	{
		Data = InData;
		ArrayNum = ArrayMax = (INT)Size;
	}
	FORCEINLINE UBOOL IsValidIndex( INT i ) const
	{
		return i>=0 && i<ArrayNum;
	}
	FORCEINLINE INT Num() const
	{
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);
		return ArrayNum;
	}
	FORCEINLINE void InsertZeroed( INT Index, INT Count, INT ElementSize, 
		INT Ratio=DEFAULT_ARRAY_SLACK_RATIO, INT Pad=DEFAULT_ARRAY_SLACK_PAD )
	{
		guardSlow(FArray::InsertZeroed);
		Insert( Index, Count, ElementSize, Ratio, Pad );
		appMemzero( (BYTE*)Data+Index*ElementSize, Count*ElementSize );
		unguardSlow;
	}
	FORCEINLINE void Insert( INT Index, INT Count, INT ElementSize, 
		INT Ratio=DEFAULT_ARRAY_SLACK_RATIO, INT Pad=DEFAULT_ARRAY_SLACK_PAD )
	{
		guardSlow(FArray::Insert);
		checkSlow(Count>=0);
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);
		checkSlow(Index>=0);
		checkSlow(Index<=ArrayNum);

		INT OldNum = ArrayNum;
		if( (ArrayNum+=Count)>ArrayMax )
		{
			ArrayMax = ArrayNum + Ratio*ArrayNum/8 + Pad;
			Realloc( ElementSize );
		}
		appMemmove
		(
			(BYTE*)Data + (Index+Count )*ElementSize,
			(BYTE*)Data + (Index       )*ElementSize,
			              (OldNum-Index)*ElementSize
		);

		unguardSlow;
	}
	FORCEINLINE INT Add( INT Count, INT ElementSize, 
		INT Ratio=DEFAULT_ARRAY_SLACK_RATIO, INT Pad=DEFAULT_ARRAY_SLACK_PAD )
	{
		guardSlow(FArray::Add);
		checkSlow(Count>=0);
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);

		INT Index = ArrayNum;
		if( (ArrayNum+=Count)>ArrayMax )
		{
			ArrayMax = ArrayNum + Ratio*ArrayNum/8 + Pad;
			Realloc( ElementSize );
		}

		return Index;
		unguardSlow;
	}
	FORCEINLINE INT AddZeroed( INT ElementSize, INT n=1, 
		INT Ratio=DEFAULT_ARRAY_SLACK_RATIO, INT Pad=DEFAULT_ARRAY_SLACK_PAD )
	{
		guardSlow(FArray::AddZeroed);
		INT Index = Add( n, ElementSize, Ratio, Pad );
		appMemzero( (BYTE*)Data+Index*ElementSize, n*ElementSize );
		return Index;
		unguardSlow;
	}
	FORCEINLINE void Shrink( INT ElementSize )
	{
		guardSlow(FArray::Shrink);
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);
		if( ArrayMax != ArrayNum )
		{
			ArrayMax = ArrayNum;
			Realloc( ElementSize );
		}
		unguardSlow;
	}
	FORCEINLINE void Empty( INT ElementSize, INT Slack=0 )
	{
		guardSlow(FArray::Empty);
		ArrayNum = 0;
		ArrayMax = Slack;
		Realloc( ElementSize );
		unguardSlow;
	}
	FORCEINLINE void Reset()
	{
		guardSlow(FArray::Reset);
		ArrayNum = 0;
		unguardSlow;
	}
	FArray()
	:	ArrayNum( 0 )
	,	ArrayMax( 0 )
	,	Data	( NULL )
	{}
	FArray( ENoInit )
	{}
	~FArray()
	{
		guardSlow(FArray::~FArray);
		if( Data )
			appFree( Data );
		Data = NULL;
		ArrayNum = ArrayMax = 0;
		unguardSlow;
	}
	FORCEINLINE void CountBytes( FArchive& Ar, INT ElementSize )
	{
		guardSlow(FArray::CountBytes);
		Ar.CountBytes( ArrayNum*ElementSize, ArrayMax*ElementSize );
		unguardSlow;
	}
	void Remove( INT Index, INT Count, INT ElementSize );
	//scion rwd - swap elements
	FORCEINLINE void SwapElements(INT A, INT B, INT ElementSize)
	{
		appMemswap((BYTE*)Data+(ElementSize*A),(BYTE*)Data+(ElementSize*B),ElementSize);
	}
protected:
	FORCEINLINE FArray::Realloc( INT ElementSize )
	{
		guardSlow(FArray::Realloc);
		Data = appRealloc( Data, ArrayMax*ElementSize );
		unguardfSlow(( TEXT("%i*%i"), ArrayMax, ElementSize ));
	}

	FArray( INT InNum, INT ElementSize )
	:	ArrayNum( InNum )
	,	ArrayMax( InNum )
	,	Data    ( NULL  )
	{
		Realloc( ElementSize );
	}
	void* Data;
	INT	  ArrayNum;
	INT	  ArrayMax;
};

//
// Templated dynamic array.
//
//scion jg -- FORCEINLINE'd all of the small accessors
//scion sz - added SlackRatio,SlackPad
template< class T, INT SlackRatio, INT SlackPad > class TArray : public FArray
{
public:
	typedef T ElementType;
	TArray()
	:	FArray()
	{}
	TArray( INT InNum )
	:	FArray( InNum, sizeof(T) )
	{}
	TArray( const TArray& Other )
	:	FArray( Other.ArrayNum, sizeof(T) )
	{
		guardSlow(TArray::copyctor);
		if( TTypeInfo<T>::NeedsDestructor() )
		{
			ArrayNum=0;
			for( INT i=0; i<Other.ArrayNum; i++ )
				new(*this)T(Other(i));
		}
		else if( sizeof(T)!=1 )
		{
			for( INT i=0; i<ArrayNum; i++ )
				(*this)(i) = Other(i);
		}
		else
		{
			appMemcpy( &(*this)(0), &Other(0), ArrayNum * sizeof(T) );
		}
		unguardSlow;
	}
	TArray( ENoInit )
	: FArray( E_NoInit )
	{}
	~TArray()
	{
		checkSlow(ArrayNum>=0);
		checkSlow(ArrayMax>=ArrayNum);
		Remove( 0, ArrayNum );
	}
	// scion sz - sets data pointer in array directly
	// add,insert,remove are all unsafe
	FORCEINLINE void SetData( void* InData, DWORD Size )
	{
		Data = InData;
		ArrayNum = ArrayMax = (INT)(Size / sizeof(T));
	}
    FORCEINLINE T& operator()( INT i )
	{
		guardSlow(TArray::operator());
		checkSlow(i>=0);
		//checkSlow(i<=ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		return ((T*)Data)[i];
		unguardSlow;
	}
	FORCEINLINE const T& operator()( INT i ) const
	{
		guardSlow(TArray::operator());
		checkSlow(i>=0);
		checkSlow(i<=ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		return ((T*)Data)[i];
		unguardSlow;
	}
	FORCEINLINE T Pop()
	{
		guardSlow(TArray::Pop);
		check(ArrayNum>0);
		checkSlow(ArrayMax>=ArrayNum);
		T Result = ((T*)Data)[ArrayNum-1];
		Remove( ArrayNum-1 );
		return Result;
		unguardSlow;
	}
	//scion bcl ----- Added Push and Top to have a complete stack interface;
	//                functionality was always there-function names document intent
	FORCEINLINE void Push( const T& Item )
	{
		guardSlow(TArray::Push);
		AddItem(Item);
		unguardSlow;
	}
	FORCEINLINE T& Top()
	{
		guardSlow(TArray::Top);
		return Last();
		unguardSlow;
	}
	FORCEINLINE const T& Top() const
	{
		guardSlow(TArray::Top);
		return Last();
		unguardSlow;
	}
	FORCEINLINE T& Last( INT c=0 )
	{
		guardSlow(TArray::Last);
		check(c<ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		return ((T*)Data)[ArrayNum-c-1];
		unguardSlow;
	}
	FORCEINLINE const T& Last( INT c=0 ) const
	{
		guardSlow(TArray::Last);
		checkSlow(c<ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		return ((T*)Data)[ArrayNum-c-1];
		unguardSlow;
	}
	FORCEINLINE void Shrink()
	{
		guardSlow(TArray::Shrink);
		FArray::Shrink( sizeof(T) );
		unguardSlow;
	}
	FORCEINLINE UBOOL FindItem( const T& Item, INT& Index ) const
	{
		guardSlow(TArray::FindItem);
		for( Index=0; Index<ArrayNum; Index++ )
			if( (*this)(Index)==Item )
				return 1;
		return 0;
		unguardSlow;
	}
	FORCEINLINE INT FindItemIndex( const T& Item ) const
	{
		guardSlow(TArray::FindItemIndex);
		for( INT Index=0; Index<ArrayNum; Index++ )
			if( (*this)(Index)==Item )
				return Index;
		return INDEX_NONE;
		unguardSlow;
	}
#if __MWERKS__
	friend FArchive& operator<<( FArchive& Ar, TArray& A );
#else
	friend FArchive& operator<<( FArchive& Ar, TArray& A )
	{
		guard(TArray<<);
		A.CountBytes( Ar );
		if( sizeof(T)==1 )
		{
			// Serialize simple bytes which require no construction or destruction.
			Ar << AR_INDEX(A.ArrayNum);
			if( Ar.IsLoading() )
			{
				A.ArrayMax = A.ArrayNum;
				A.Realloc( sizeof(T) );
			}
			Ar.Serialize( &A(0), A.Num() );
		}
		else if( Ar.IsLoading() )
		{
			// Load array.
			INT NewNum;
			Ar << AR_INDEX(NewNum);
			A.Empty( NewNum );
			for( INT i=0; i<NewNum; i++ )
				Ar << *new(A)T;
		}
		else
		{
			// Save array.
			Ar << AR_INDEX(A.ArrayNum);
			for( INT i=0; i<A.ArrayNum; i++ )
				Ar << A( i );
		}
		return Ar;
		unguard;
	}
#endif
	FORCEINLINE void CountBytes( FArchive& Ar )
	{
		guardSlow(TArray::CountBytes);
		FArray::CountBytes( Ar, sizeof(T) );
		unguardSlow;
	}

	// Add, Insert, Remove, Empty interface.
	FORCEINLINE INT Add( INT n=1 )
	{
		guardSlow(TArray::Add);
		checkSlow(!TTypeInfo<T>::DefinitelyNeedsDestructor());
		return FArray::Add( n, sizeof(T), SlackRatio, SlackPad );
		unguardSlow;
	}

	// Reserves space such that the array can contain at least n elements. Returns true if new space was allocated.
	FORCEINLINE bool Reserve(INT n)
	{
		guardSlow(TArray::Reserve);
		if (n > ArrayMax)
		{
			Add(n - ArrayMax);
			return true;
		}
		else
		{
			return false;
		}
		unguardSlow;
	}

	// Releases the array. The caller becomes responsible for deallocating the array.
	FORCEINLINE T* Release()
	{
		guardSlow(TArray::Release);
		T* ReleasedData = (T*) Data;
		ArrayNum = ArrayMax = 0;
		Data = NULL;
		return ReleasedData;
		unguardSlow;
	}

	FORCEINLINE void Insert( INT Index, INT Count=1 )
	{
		guardSlow(TArray::Insert);
		checkSlow(!TTypeInfo<T>::DefinitelyNeedsDestructor());
		FArray::Insert( Index, Count, sizeof(T), SlackRatio, SlackPad );
		unguardSlow;
	}
	FORCEINLINE void InsertZeroed( INT Index, INT Count=1 )
	{
		guardSlow(TArray::InsertZeroed);
		checkSlow(!TTypeInfo<T>::DefinitelyNeedsDestructor());
		FArray::InsertZeroed( Index, Count, sizeof(T), SlackRatio, SlackPad );
		unguardSlow;
	}
	FORCEINLINE void Remove( INT Index, INT Count=1 )
	{
		guardSlow(TArray::Remove);
		check(Index >= 0);
		check(Index <= ArrayNum);
		check(Index + Count <= ArrayNum);
		if( TTypeInfo<T>::NeedsDestructor() )
			for( INT i=Index; i<Index+Count; i++ )
				(&(*this)(i))->~T();
		FArray::Remove( Index, Count, sizeof(T) );
		unguardSlow;
	}
	//scion bcl -- Swap Elements with right size.
	FORCEINLINE void SwapElements(INT A, INT B)
	{
		guardSlow(TArray::SwapElements);
		check((A >= 0) && (B >= 0));
		check((ArrayNum > A) && (ArrayNum > B));
		FArray::SwapElements(A, B, sizeof(T));
		unguardSlow;
	}
	FORCEINLINE void Empty( INT Slack=0 )
	{
		guardSlow(TArray::Empty);
		if( TTypeInfo<T>::NeedsDestructor() )
			for( INT i=0; i<ArrayNum; i++ )
				(&(*this)(i))->~T();
		FArray::Empty( sizeof(T), Slack );
		unguardSlow;
	}
	FORCEINLINE void Reset()
	{
		guardSlow(TArray::Reset);
		FArray::Reset();
		unguardSlow;
	}

	/**
	 * jg -- Fast copy of one array into this array. Assumes that no ctor is
	 * needed for items. Use at your own risk.
	 */
	FORCEINLINE void FastCopy(const TArray& Other)
	{
		guard(TArray::FastCopy);
		checkSlow(&Other != this);
		// Don't try to memcpy from NULL data
		if (Other.Data != NULL)
		{
			// Do a smarter assignment of the data. First allocate the space
			// then do a memcpy rather than an array iteration
			Empty(Other.ArrayNum);
			appMemcpy(Data,Other.Data,Other.ArrayNum * sizeof(T));
			ArrayNum = Other.ArrayNum;
		}
		else
		{
			// Just zero our array
			Empty();
		}
		unguard;
	}

	// Functions dependent on Add, Remove.
	FORCEINLINE TArray& operator+( const TArray& Other )
	{
		guardSlow(TArray::operator=);
		if( this != &Other )
		{
			for( INT i=0; i<Other.ArrayNum; i++ )
				new( *this )T( Other(i) );
		}
		return *this;
		unguardSlow;
	}
	FORCEINLINE TArray& operator+=( const TArray& Other )
	{
		guardSlow(TArray::operator=);
		if( this != &Other )
		{
			*this = *this + Other;
		}
		return *this;
		unguardSlow;
	}
	FORCEINLINE TArray& operator=( const TArray& Other )
	{
		guardSlow(TArray::operator=);
		if( this != &Other )
		{
			Empty( Other.ArrayNum );
			for( INT i=0; i<Other.ArrayNum; i++ )
				new( *this )T( Other(i) );
		}
		return *this;
		unguardSlow;
	}
	FORCEINLINE INT AddItem( const T& Item )
	{
		guardSlow(TArray::AddItem);
		checkSlow(!TTypeInfo<T>::DefinitelyNeedsDestructor());
		INT Index=Add();
		(*this)(Index)=Item;
		return Index;
		unguardSlow;
	}
	FORCEINLINE INT AddZeroed( INT n=1 )
	{
		guardSlow(TArray::AddZeroed);
		return FArray::AddZeroed( sizeof(T), n, SlackRatio, SlackPad );
		unguardSlow;
	}
	FORCEINLINE INT AddUniqueItem( const T& Item )
	{
		guardSlow(TArray::AddUniqueItem);
		checkSlow(!TTypeInfo<T>::DefinitelyNeedsDestructor());
		for( INT Index=0; Index<ArrayNum; Index++ )
			if( (*this)(Index)==Item )
				return Index;
		return AddItem( Item );
		unguardSlow;
	}
	FORCEINLINE INT RemoveItem( const T& Item )
	{
		guardSlow(TArray::RemoveItem);
		INT OriginalNum=ArrayNum;
		for( INT Index=0; Index<ArrayNum; Index++ )
			if( (*this)(Index)==Item )
				Remove( Index-- );
		return OriginalNum - ArrayNum;
		unguardSlow;
	}
	// Iterator.
	class TIterator
	{
	public:
		TIterator( TArray& InArray ) : Array(InArray), Index(-1) { ++*this;      }
		void operator++()      { ++Index;                                           }
		void RemoveCurrent()   { Array.Remove(Index--); }
		INT GetIndex()   const { return Index;                                      }
		operator UBOOL() const { return Index < Array.Num();                        }
		T& operator*()   const { return Array(Index);                               }
		T* operator->()  const { return &Array(Index);                              }
		T& GetCurrent()  const { return Array( Index );                             }
		T& GetPrev()     const { return Array( Index ? Index-1 : Array.Num()-1 );   }
		T& GetNext()     const { return Array( Index<Array.Num()-1 ? Index+1 : 0 ); }
	private:
		TArray& Array;
		INT Index;
	};


#if (defined _MSC_VER) && (defined _DEBUG)
	/**
	 * Helper function that can be used inside the debuggers watch window to debug TArrays. E.g. "*Class->Defaults.DebugGet(5)". 
	 *
	 * @param	i	Index
	 * @return		pointer to type T at Index i
	 */
	const T* DebugGet( INT i ) const
	{
		if( i >= 0 )
			return &((T*)Data)[i];
		else
			return NULL;
	}
#endif
};

#if __MWERKS__
template< class T > inline FArchive& operator<<( FArchive& Ar, TArray<T>& A )
{
	guard(TArray<<);
	A.CountBytes( Ar );
	if( sizeof(T)==1 )
	{
		// Serialize simple bytes which require no construction or destruction.
		Ar << AR_INDEX(A.ArrayNum);
		if( Ar.IsLoading() )
		{
			A.ArrayMax = A.ArrayNum;
			A.Realloc( sizeof(T) );
		}
		Ar.Serialize( &A(0), A.Num() );
	}
	else if( Ar.IsLoading() )
	{
		// Load array.
		INT NewNum;
		Ar << AR_INDEX(NewNum);
		A.Empty( NewNum );
		for( INT i=0; i<NewNum; i++ )
			Ar << *new(A)T;
	}
	else
	{
		// Save array.
		Ar << AR_INDEX(A.ArrayNum);
		for( INT i=0; i<A.ArrayNum; i++ )
			Ar << A( i );
	}
	return Ar;
	unguard;
}

// Since the above doesn't seem to actually do anything, I'm also including this
// handy macro to make manually instantiating these things easier.

#define INSTANTIATE_TEMPLATE(TypeName) \
	FArchive& operator<<( FArchive& Ar, TArray<TypeName>& A ) \
	{ \
		A.CountBytes( Ar ); \
		if( sizeof(TypeName)==1 ) \
		{ \
			Ar << AR_INDEX(A.ArrayNum); \
			if( Ar.IsLoading() ) \
			{ \
				A.ArrayMax = A.ArrayNum; \
				A.Realloc( sizeof(TypeName) ); \
			} \
			Ar.Serialize( &A(0), A.Num() ); \
		} \
		else if( Ar.IsLoading() ) \
		{ \
			INT NewNum; \
			Ar << AR_INDEX(NewNum); \
			A.Empty( NewNum ); \
			for( INT i=0; i<NewNum; i++ ) \
				Ar << *new(A)TypeName; \
		} \
		else \
		{ \
			Ar << AR_INDEX(A.ArrayNum); \
			for( INT i=0; i<A.ArrayNum; i++ ) \
				Ar << A(i); \
		} \
		return Ar; \
	}

#endif
template<class T> class TArrayNoInit : public TArray<T>
{
public:
	TArrayNoInit()
	: TArray<T>(E_NoInit)
	{}
	TArrayNoInit& operator=( const TArrayNoInit& Other )
	{
		TArray<T>::operator=(Other);
		return *this;
	}
};

//
// Array operator news.
//
template <class T> void* operator new( size_t Size, TArray<T>& Array )
{
	guardSlow(TArray::operator new);
	INT Index = Array.FArray::Add(1,sizeof(T));
	return &Array(Index);
	unguardSlow;
}
template <class T> void* operator new( size_t Size, TArray<T>& Array, INT Index )
{
	guardSlow(TArray::operator new);
	Array.FArray::Insert(Index,1,sizeof(T));
	return &Array(Index);
	unguardSlow;
}

#if __GNUG__
//!!vogel: gcc needs a little help (explicit instantiation) in this case.
template void* operator new( size_t Size, TArray<FString>& Array );
#endif

//
// Array exchanger.
//
template <class T> inline void ExchangeArray( TArray<T>& A, TArray<T>& B )
{
	guardSlow(ExchangeTArray);
	appMemswap( &A, &B, sizeof(FArray) );
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	Transactional array.
-----------------------------------------------------------------------------*/

template< class T > class TTransArray : public TArray<T>
{
public:
	// Constructors.
	TTransArray( UObject* InOwner, INT InNum=0 )
	:	TArray<T>( InNum )
	,	Owner( InOwner )
	{
		checkSlow(Owner);
	}
	TTransArray( UObject* InOwner, const TArray<T>& Other )
	:	TArray<T>( Other )
	,	Owner( InOwner )
	{
		checkSlow(Owner);
	}
	TTransArray& operator=( const TTransArray& Other )
	{
		operator=( (const TArray<T>&)Other );
		return *this;
	}

	// Add, Insert, Remove, Empty interface.
	INT Add( INT Count=1 )
	{
		guardSlow(TTransArray::Add);
		INT Index = TArray<T>::Add( Count );
		if( GUndo )
			GUndo->SaveArray( Owner, this, Index, Count, 1, sizeof(T), SerializeItem, DestructItem );
		return Index;
		unguardSlow;
	}
	void Insert( INT Index, INT Count=1 )
	{
		guardSlow(TTransArray::InsertZeroed);
		FArray::Insert( Index, Count, sizeof(T) );
		if( GUndo )
			GUndo->SaveArray( Owner, this, Index, Count, 1, sizeof(T), SerializeItem, DestructItem );
		unguardSlow;
	}
	void Remove( INT Index, INT Count=1 )
	{
		guardSlow(TTransArray::Remove);
		if( GUndo )
			GUndo->SaveArray( Owner, this, Index, Count, -1, sizeof(T), SerializeItem, DestructItem );
		TArray<T>::Remove( Index, Count );
		unguardSlow;
	}
	void Empty( INT Slack=0 )
	{
		guardSlow(TTransArray::Empty);
		if( GUndo )
			GUndo->SaveArray( Owner, this, 0, ArrayNum, -1, sizeof(T), SerializeItem, DestructItem );
		TArray<T>::Empty( Slack );
		unguardSlow;
	}

	// Functions dependent on Add, Remove.
	TTransArray& operator=( const TArray<T>& Other )
	{
		guardSlow(TTransArray::operator=);
		if( this != &Other )
		{
			Empty( Other.Num() );
			for( INT i=0; i<Other.Num(); i++ )
				new( *this )T( Other(i) );
		}
		return *this;
		unguardSlow;
	}
	INT AddItem( const T& Item )
	{
		guardSlow(TTransArray::AddItem);
		INT Index=Add();
		(*this)(Index)=Item;
		return Index;
		unguardSlow;
	}
	INT AddZeroed( INT n=1 )
	{
		guardSlow(TTransArray::AddZeroed);
		INT Index = Add(n);
		appMemzero( &(*this)(Index), n*sizeof(T) );
		return Index;
		unguardSlow;
	}
	INT AddUniqueItem( const T& Item )
	{
		guardSlow(TTransArray::AddUniqueItem);
		for( INT Index=0; Index<ArrayNum; Index++ )
			if( (*this)(Index)==Item )
				return Index;
		return AddItem( Item );
		unguardSlow;
	}
	INT RemoveItem( const T& Item )
	{
		guardSlow(TTransArray::RemoveItem);
		INT OriginalNum=ArrayNum;
		for( INT Index=0; Index<ArrayNum; Index++ )
			if( (*this)(Index)==Item )
				Remove( Index-- );
		return OriginalNum - ArrayNum;
		unguardSlow;
	}

	// TTransArray interface.
	UObject* GetOwner()
	{
		return Owner;
	}
	void ModifyItem( INT Index )
	{
		guardSlow(TTransArray::ModifyItem);
		if( GUndo )
			GUndo->SaveArray( Owner, this, Index, 1, 0, sizeof(T), SerializeItem, DestructItem );
		unguardSlow;
	}
	void ModifyAllItems()
	{
		guardSlow(TTransArray::ModifyAllItems);
		if( GUndo )
			GUndo->SaveArray( Owner, this, 0, Num(), 0, sizeof(T), SerializeItem, DestructItem );
		unguardSlow;
	}
	friend FArchive& operator<<( FArchive& Ar, TTransArray& A )
	{
		guard(TTransArray<<);
		if( !Ar.IsTrans() )
			Ar << (TArray<T>&)A;
		return Ar;
		unguard;
	}
protected:
	static void SerializeItem( FArchive& Ar, void* TPtr )
	{
		guardSlow(TArray::SerializeItem);
		Ar << *(T*)TPtr;
		unguardSlow;
	}
	static void DestructItem( void* TPtr )
	{
		guardSlow(TArray::SerializeItem);
		((T*)TPtr)->~T();
		unguardSlow;
	}
	UObject* Owner;
private:

	// Disallow the copy constructor.
	TTransArray( const TArray<T>& Other )
	{}
};

//
// Transactional array operator news.
//
template <class T> void* operator new( size_t Size, TTransArray<T>& Array )
{
	guardSlow(TArray::operator new);
	INT Index = Array.Add();
	return &Array(Index);
	unguardSlow;
}
template <class T> void* operator new( size_t Size, TTransArray<T>& Array, INT Index )
{
	guardSlow(TArray::operator new);
	Array.Insert(Index);
	return &Array(Index);
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	Lazy loading.
-----------------------------------------------------------------------------*/

//
// Lazy loader base class.
//
class FLazyLoader
{
	friend class ULinkerLoad;
protected:
	FArchive*	 SavedAr;
	INT          SavedPos;
public:
	FLazyLoader()
	: SavedAr( NULL )
	, SavedPos( 0 )
	{}
	virtual void Load()=0;
	virtual void Unload()=0;
};

//
// Lazy-loadable dynamic array.
//
template <class T> class TLazyArray : public TArray<T>, public FLazyLoader
{
public:
	TLazyArray( INT InNum=0 )
	: TArray<T>( InNum )
	, FLazyLoader()
	{}
	~TLazyArray()
	{
		guard(TLazyArray::~TLazyArray);
		if( SavedAr )
			SavedAr->DetachLazyLoader( this );
		unguard;
	}
#if LOAD_ON_DEMAND /* Breaks because of untimely accesses of operator() !! */
    T& operator()( INT i )
	{
		guardSlow(TArray::operator());
		checkSlow(i>=0);
		checkSlow(i<=ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		if( SavedPos>0 )
			Load();
		return ((T*)Data)[i];
		unguardSlow;
	}
	const T& operator()( INT i ) const
	{
		guardSlow(TArray::operator());
		checkSlow(i>=0);
		checkSlow(i<=ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		if( SavedPos>0 )
			Load();
		return ((T*)Data)[i];
		unguardSlow;
	}
#endif
	void Load()
	{
		// Make sure this array is loaded.
		guard(TLazyArray::Load);
		if( SavedPos>0 )
		{
			// Lazy load it now.
			INT PushedPos = SavedAr->Tell();
			SavedAr->Seek( SavedPos );
			*SavedAr << (TArray<T>&)*this;
			SavedPos *= -1;
			SavedAr->Seek( PushedPos );
		}
		unguard;
	}
	void Unload()
	{
		// Make sure this array is unloaded.
		guard(TLazyArray::Unload);
		if( SavedPos<0 )
		{
			// Unload it now.
			Empty();
			SavedPos *= -1;
		}
		unguard;
	}
	
	friend FArchive& operator<<( FArchive& Ar, TLazyArray& This )
	{
		guard(TLazyArray<<);
		if( Ar.IsLoading() )
		{
			INT SeekPos=0;
			if( Ar.Ver() <= 61 )
			{
				//oldver: Handles dynamic arrays of fixed-length serialized items only.
				Ar.AttachLazyLoader( &This );
				INT SkipCount;
				Ar << AR_INDEX(SkipCount);
				SeekPos = Ar.Tell() + SkipCount*sizeof(T);
					
			}
			else
			{
				Ar << SeekPos;
				if( GUglyHackFlags & 8 )
					Ar << (TArray<T>&)This;		
				else
					Ar.AttachLazyLoader( &This );
			}
			if( !GLazyLoad )
				This.Load();
			Ar.Seek( SeekPos );
		}
		else if( Ar.IsSaving() && Ar.Ver()>61 )
		{
			// Save out count for skipping over this data.
			INT CountPos = Ar.Tell();
			Ar << CountPos << (TArray<T>&)This;
			INT EndPos = Ar.Tell();
			Ar.Seek( CountPos );
			Ar << EndPos;
			Ar.Seek( EndPos );
		}
		else Ar << (TArray<T>&)This;
		return Ar;
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	Dynamic strings.
-----------------------------------------------------------------------------*/

//
// A dynamically sizeable string.
//
class CORE_API FString : protected TArray<TCHAR>
{
public:
	FString()
	: TArray<TCHAR>()
	{}
	FString( const FString& Other )
	: TArray<TCHAR>( Other.ArrayNum )
	{
		if( ArrayNum )
			appMemcpy( &(*this)(0), &Other(0), ArrayNum*sizeof(TCHAR) );
	}
	FString( const TCHAR* In )
	: TArray<TCHAR>( *In ? (appStrlen(In)+1) : 0 )
	{
		if( ArrayNum )
			appMemcpy( &(*this)(0), In, ArrayNum*sizeof(TCHAR) );
	}
	FString( ENoInit )
	: TArray<TCHAR>( E_NoInit )
	{}
	explicit FString( BYTE   Arg, INT Digits=1 );
	explicit FString( SBYTE  Arg, INT Digits=1 );
	explicit FString( _WORD  Arg, INT Digits=1 );
	explicit FString( SWORD  Arg, INT Digits=1 );
	explicit FString( INT    Arg, INT Digits=1 );
	explicit FString( DWORD  Arg, INT Digits=1 );
	explicit FString( FLOAT  Arg, INT Digits=1, INT RightDigits=0, UBOOL LeadZero=1 );
#ifndef __PSX2_EE__
	explicit FString( DOUBLE Arg, INT Digits=1, INT RightDigits=0, UBOOL LeadZero=1 );
#endif
	FString& operator=( const TCHAR* Other )
	{
		if( &(*this)(0)!=Other )
		{
			ArrayNum = ArrayMax = *Other ? appStrlen(Other)+1 : 0;
			Realloc( sizeof(TCHAR) );
			if( ArrayNum )
				appMemcpy( &(*this)(0), Other, ArrayNum*sizeof(TCHAR) );
		}
		return *this;
	}
	FString& operator=( const FString& Other )
	{
		if( this != &Other )
		{
			ArrayNum = ArrayMax = Other.Num();
			Realloc( sizeof(TCHAR) );
			if( ArrayNum )
				appMemcpy( &(*this)(0), *Other, ArrayNum*sizeof(TCHAR) );
		}
		return *this;
	}
    TCHAR& operator[]( INT i )
	{
		guardSlow(FString::operator());
		checkSlow(i>=0);
		checkSlow(i<=ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		return ((TCHAR*)Data)[i];
		unguardSlow;
	}
	const TCHAR& operator[]( INT i ) const
	{
		guardSlow(FString::operator());
		checkSlow(i>=0);
		checkSlow(i<=ArrayNum);
		checkSlow(ArrayMax>=ArrayNum);
		return ((TCHAR*)Data)[i];
		unguardSlow;
	}

	~FString()
	{
		TArray<TCHAR>::Empty();		
	}
	void Empty()
	{
		TArray<TCHAR>::Empty();
	}
	void Shrink()
	{
		TArray<TCHAR>::Shrink();
	}
	const TCHAR* operator*() const
	{
		return Num() ? &(*this)(0) : TEXT("");
	}
	operator UBOOL() const
	{
		return Num()!=0;
	}
	TArray<TCHAR>& GetCharArray()
	{
		//warning: Operations on the TArray<CHAR> can be unsafe, such as adding
		// non-terminating 0's or removing the terminating zero.
		return (TArray<TCHAR>&)*this;
	}
	FString& operator+=( const TCHAR* Str )
	{
		if( *Str != '\0' )
		{
		    if( ArrayNum )
		    {
			    INT Index = ArrayNum-1;
			    Add( appStrlen(Str) );
			    appStrcpy( &(*this)(Index), Str );
		    }
		    else if( *Str )
		    {
			    Add( appStrlen(Str)+1 );
			    appStrcpy( &(*this)(0), Str );
		    }
		}
		return *this;
	}
	FString& operator+=( const FString& Str )
	{
		return operator+=( *Str );
	}
	FString& operator +=( const TCHAR ch )
	{
		if ( ch != '\0' ) {
			INT Index = (ArrayNum)?ArrayNum-1:0; // where to insert the character
			INT nChar = (ArrayNum)?1:2; // number of characters to add (need to worry about \0

			Add(nChar);
			(*this)(Index) = ch;
			(*this)(Index+1) = '\0';
		}
		return *this;
	}
	FString operator+( const TCHAR* Str )const
	{
		return FString( *this ) += Str;
	}
	FString operator+( const FString& Str ) const
	{
		return operator+( *Str );
	}
	FString operator+( const TCHAR Ch ) const
	{
		return FString( *this) += Ch;
	}
	FString& operator*=( const TCHAR* Str )
	{
		if( ArrayNum>1 && (*this)(ArrayNum-2)!=PATH_SEPARATOR[0] )
			*this += PATH_SEPARATOR;
		return *this += Str;
	}
	FString& operator*=( const FString& Str )
	{
		return operator*=( *Str );
	}
	FString operator*( const TCHAR* Str ) const
	{
		return FString( *this ) *= Str;
	}
	FString operator*( const FString& Str ) const
	{
		return operator*( *Str );
	}
	UBOOL operator<=( const TCHAR* Other ) const
	{
		return !(appStricmp( **this, Other ) > 0);
	}
	UBOOL operator<=( const FString& Other ) const
	{
		return !(appStricmp( **this, *Other ) > 0);
	}
	UBOOL operator<( const TCHAR* Other ) const
	{
		return appStricmp( **this, Other ) < 0;
	}
	UBOOL operator<( const FString& Other ) const
	{
		return appStricmp( **this, *Other ) < 0;
	}
	UBOOL operator>=( const TCHAR* Other ) const
	{
		return !(appStricmp( **this, Other ) < 0);
	}
	UBOOL operator>=( const FString& Other ) const
	{
		return !(appStricmp( **this, *Other ) < 0);
	}
	UBOOL operator>( const TCHAR* Other ) const
	{
		return appStricmp( **this, Other ) > 0;
	}
	UBOOL operator>( const FString& Other ) const
	{
		return appStricmp( **this, *Other ) > 0;
	}
	UBOOL operator==( const TCHAR* Other ) const
	{
		return appStricmp( **this, Other )==0;
	}
	UBOOL operator==( const FString& Other ) const
	{
		return appStricmp( **this, *Other )==0;
	}
	UBOOL operator!=( const TCHAR* Other ) const
	{
		return appStricmp( **this, Other )!=0;
	}
	UBOOL operator!=( const FString& Other ) const
	{
		return appStricmp( **this, *Other )!=0;
	}
	INT Len() const
	{
		return Num() ? Num()-1 : 0;
	}
	FString Left( INT Count ) const
	{
		return FString( Clamp(Count,0,Len()), **this );
	}
	FString LeftChop( INT Count ) const
	{
		return FString( Clamp(Len()-Count,0,Len()), **this );
	}
	FString Right( INT Count ) const
	{
		return FString( **this + Len()-Clamp(Count,0,Len()) );
	}
	FString Mid( INT Start, INT Count=MAXINT ) const
	{
		DWORD End = Start+Count;
		Start    = Clamp( (DWORD)Start, (DWORD)0,     (DWORD)Len() );
		End      = Clamp( (DWORD)End,   (DWORD)Start, (DWORD)Len() );
		return FString( End-Start, **this + Start );
	}
	FString RemoveSpaces() const
	{
		FString Reduced(*this);
		FString Left, Right;
		while (Reduced.Split(TEXT(" "), &Left, &Right))
		{
			Reduced = Left + Right;
		}
		return Reduced;
	}
	INT InStr( const TCHAR* SubStr, UBOOL Right=0 ) const
	{
		if( !Right )
		{
			TCHAR* Tmp = appStrstr(**this,SubStr);
			return Tmp ? (Tmp-**this) : -1;
		}
		else
		{
			for( INT i=Len()-1; i>=0; i-- )
			{
				INT j;
				for( j=0; SubStr[j]; j++ )
					if( (*this)(i+j)!=SubStr[j] )
						break;
				if( !SubStr[j] )
					return i;
			}
			return -1;
		}
	}
	INT InStr( const FString& SubStr, UBOOL Right=0 ) const
	{
		return InStr( *SubStr, Right );
	}
	UBOOL Split( const FString& InS, FString* LeftS, FString* RightS, UBOOL Right=0 ) const
	{
		INT InPos = InStr(InS,Right);
		if( InPos<0 )
			return 0;
		if( LeftS )
			*LeftS = Left(InPos);
		if( RightS )
			*RightS = Mid(InPos+InS.Len());
		return 1;
	}
	FString Caps() const
	{
		FString New( **this );
		for( INT i=0; i<New.ArrayNum; i++ )
			New(i) = appToUpper(New(i));
		return New;
	}
	FString Locs() const
	{
		FString New( **this );
		for( INT i=0; i<New.ArrayNum; i++ )
			New(i) = appToLower(New(i));
		return New;
	}
	FString LeftPad( INT ChCount );
	FString RightPad( INT ChCount );
	static FString Printf( const TCHAR* Fmt, ... );
	static FString Chr( TCHAR Ch );
	CORE_API friend FArchive& operator<<( FArchive& Ar, FString& S );
	friend struct FStringNoInit;
	// Breaks up this delimited string into elements of a string array.
	INT ParseIntoArray( const TCHAR* pchDelim, TArray<FString>* InArray)
	{
		guard(FString::ParseIntoArray);
		check(InArray);

		FString S = *this;

		for( INT i = S.InStr( pchDelim ) ; i > 0 ; )
		{
			new(*InArray)FString( S.Left(i) );
			S = S.Mid( i + 1, S.Len() );
			i = S.InStr( pchDelim );
		}

		new(*InArray)FString( S );
		return InArray->Num();
		unguard;
	}
	//scion bcl - modified to be a const function (not modifying *this) since it is always used as a returning value
	//            more consistent with other functions here, too.
	// Reverses the string
	FString Reverse() const
	{
		guard(FString::ReverseG);
		FString New;
		for( int x = Len()-1 ; x > -1 ; x-- )
			New += Mid(x,1);
		//*this = New;
		return New;
		unguard;
	}
	
	// replace all occurances of the string "tag" with "repl"; return a copy of the  string if tag is empty or not found
	FString Replace(const FString & tag, const FString & repl)
	{
		guard(FString::Replace);
		INT offset = InStr(*tag);
		INT tLen = tag.Len();
		if ((offset >= 0) && (tLen > 0)) {
			FString retval = Left(offset) + repl;
			FString copyOfThis = Mid(offset+tLen);
			offset = copyOfThis.InStr(*tag);
			while (offset >= 0) {
				retval += copyOfThis.Left(offset) + repl;
				copyOfThis = copyOfThis.Mid(offset+tLen);
				offset = copyOfThis.InStr(*tag);
			}
			return retval+copyOfThis;
		}
		return *this;
		unguard;
	} // Replace

	// Takes the number passed in and formats the string in comma format ( 12345 becomes "12,345")
	static FString FormatAsNumber( INT InNumber )
	{
		FString Number = appItoa( InNumber ), Result;

		int dec = 0;
		for( int x = Number.Len()-1 ; x > -1 ; --x )
		{
			Result += Number.Mid(x,1);

			dec++;
			if( dec == 3 && x > 0 )
			{
				Result += TEXT(",");
				dec = 0;
			}
		}

		return Result.Reverse();
	}
private:
	FString( INT InCount, const TCHAR* InSrc )
	:	TArray<TCHAR>( InCount ? InCount+1 : 0 )
	{
		if( ArrayNum )
			appStrncpy( &(*this)(0), InSrc, InCount+1 );
	}
};
struct CORE_API FStringNoInit : public FString
{
	FStringNoInit()
	: FString( E_NoInit )
	{}
	FStringNoInit& operator=( const TCHAR* Other )
	{
		if( &(*this)(0)!=Other )
		{
			ArrayNum = ArrayMax = *Other ? appStrlen(Other)+1 : 0;
			Realloc( sizeof(TCHAR) );
			if( ArrayNum )
				appMemcpy( &(*this)(0), Other, ArrayNum*sizeof(TCHAR) );
		}
		return *this;
	}
	FStringNoInit& operator=( const FString& Other )
	{
		if( this != &Other )
		{
			ArrayNum = ArrayMax = Other.Num();
			Realloc( sizeof(TCHAR) );
			if( ArrayNum )
				appMemcpy( &(*this)(0), *Other, ArrayNum*sizeof(TCHAR) );
		}
		return *this;
	}
};
inline DWORD GetTypeHash( const FString& S )
{
	return appStrihash(*S);
}
template <> struct TTypeInfo<FString> : public TTypeInfoBase<FString>
{
	typedef const TCHAR* ConstInitType;
	static const TCHAR* ToInit( const FString& In ) {return *In;}
	static UBOOL DefinitelyNeedsDestructor() {return 0;}
};

//
// String exchanger.
//
inline void ExchangeString( FString& A, FString& B )
{
	guardSlow(ExchangeTArray);
	appMemswap( &A, &B, sizeof(FString) );
	unguardSlow;
}

/*----------------------------------------------------------------------------
	Special archivers.
----------------------------------------------------------------------------*/

//
// String output device.
//
class FStringOutputDevice : public FString, public FOutputDevice
{
public:
	FStringOutputDevice( const TCHAR* InStr=TEXT("") )
	: FString( InStr )
	{}
	void Serialize( const TCHAR* Data, EName Event )
	{
		*this += (TCHAR*)Data;
	}
};

//
// Buffer writer.
//
class FBufferWriter : public FArchive
{
public:
	FBufferWriter( TArray<BYTE>& InBytes )
	: Bytes( InBytes )
	, Pos( 0 )
	{
		ArIsSaving = 1;
	}
	void Serialize( void* InData, INT Length )
	{
		if( Pos+Length>Bytes.Num() )
			Bytes.Add( Pos+Length-Bytes.Num() );
		appMemcpy( &Bytes(Pos), InData, Length );
		Pos += Length;
	}
	INT Tell()
	{
		return Pos;
	}
	void Seek( INT InPos )
	{
		Pos = InPos;
	}
	INT TotalSize()
	{
		return Bytes.Num();
	}
private:
	TArray<BYTE>& Bytes;
	INT Pos;
};

//
// Buffer archiver.
//
class FBufferArchive : public FBufferWriter, public TArray<BYTE>
{
public:
	FBufferArchive()
	: FBufferWriter( (TArray<BYTE>&)*this )
	{}
};

//
// Buffer reader.
//
class CORE_API FBufferReader : public FArchive
{
public:
	FBufferReader( const TArray<BYTE>& InBytes )
	:	Bytes	( InBytes )
	,	Pos 	( 0 )
	{
		ArIsLoading = ArIsTrans = 1;
	}
	void Serialize( void* Data, INT Num )
	{
		check(Pos>=0);
		check(Pos+Num<=Bytes.Num());
		appMemcpy( Data, &Bytes(Pos), Num );
		Pos += Num;
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Bytes.Num();
	}
	void Seek( INT InPos )
	{
		check(InPos>=0);
		check(InPos<=Bytes.Num());
		Pos = InPos;
	}
	UBOOL AtEnd()
	{
		return Pos>=Bytes.Num();
	}
private:
	const TArray<BYTE>& Bytes;
	INT Pos;
};

/*----------------------------------------------------------------------------
	TMap.
----------------------------------------------------------------------------*/

//
// Maps unique keys to values.
//
//scion jg -- Added the third parameter so I could control the minimum hash size
template< class TK, class TI, INT MinHashSize = 8 > class TMapBase
{
protected:
	class TPair
	{
	public:
		INT HashNext;
		TK Key;
		TI Value;
		TPair( typename TTypeInfo<TK>::ConstInitType InKey, typename TTypeInfo<TI>::ConstInitType InValue )
		: Key( InKey ), Value( InValue )
		{}
		TPair()
		{}
		friend FArchive& operator<<( FArchive& Ar, TPair& F )
		{
			guardSlow(TMapBase::TPair<<);
			return Ar << F.Key << F.Value;
			unguardSlow;
		}
	};
	void Rehash()
	{
		guardSlow(TMapBase::Rehash);
		checkSlow(!(HashCount&(HashCount-1)));
		checkSlow(HashCount >= MinHashSize);
		INT* NewHash = new INT[HashCount];
		{for( INT i=0; i<HashCount; i++ )
		{
			NewHash[i] = INDEX_NONE;
		}}
		{for( INT i=0; i<Pairs.Num(); i++ )
		{
			TPair& Pair    = Pairs(i);
			INT    iHash   = (GetTypeHash(Pair.Key) & (HashCount-1));
			Pair.HashNext  = NewHash[iHash];
			NewHash[iHash] = i;
		}}
		if( Hash )
			delete[] Hash;
		Hash = NewHash;
			unguardSlow;
	}
	FORCEINLINE void AllocHash()
	{
		if(!Hash)
			Rehash();
	}
	void Relax()
	{
		guardSlow(TMapBase::Relax);
		while( HashCount>Pairs.Num() * 2 + MinHashSize )
			HashCount /= 2;
		Rehash();
		unguardSlow;
	}
	TI& Add( typename TTypeInfo<TK>::ConstInitType InKey, typename TTypeInfo<TI>::ConstInitType InValue )
	{
		guardSlow(TMapBase::Add);
		AllocHash();
		TPair& Pair   = *new(Pairs)TPair( InKey, InValue );
		INT    iHash  = (GetTypeHash(Pair.Key) & (HashCount-1));
		Pair.HashNext = Hash[iHash];
		Hash[iHash]   = Pairs.Num()-1;
		if( HashCount * 2 + MinHashSize < Pairs.Num() )
		{
			HashCount *= 2;
			Rehash();
		}
		return Pair.Value;
		unguardSlow;
	}
	TArray<TPair> Pairs;
	INT* Hash;
	INT HashCount;
public:
	TMapBase()
	:	Hash( NULL )
	,	HashCount( MinHashSize )
	{
	}
	TMapBase( const TMapBase& Other )
	:	Pairs( Other.Pairs )
	,	HashCount( Other.HashCount )
	,	Hash( NULL )
	{
		guardSlow(TMapBase::TMapBase copy);
		Rehash();
		unguardSlow;
	}
	~TMapBase()
	{
		guardSlow(TMapBase::~TMapBase);
		if( Hash )
			delete Hash;
		Hash = NULL;
		HashCount = 0;
		unguardSlow;
	}
	TMapBase& operator=( const TMapBase& Other )
	{
		guardSlow(TMapBase::operator=);
		Pairs     = Other.Pairs;
		HashCount = Other.HashCount;
		Rehash();
		return *this;
		unguardSlow;
	}
	void Empty()
	{
		guardSlow(TMapBase::Empty);
		checkSlow(!(HashCount&(HashCount-1)));
		Pairs.Empty();
		HashCount = MinHashSize;
		Rehash();
		unguardSlow;
	}

	TI& Set( typename TTypeInfo<TK>::ConstInitType InKey, typename TTypeInfo<TI>::ConstInitType InValue )
	{
		AllocHash();
		for( INT i=Hash[(GetTypeHash(InKey) & (HashCount-1))]; i!=INDEX_NONE; i=Pairs(i).HashNext )
			if( Pairs(i).Key==InKey )
				{Pairs(i).Value=InValue; return Pairs(i).Value;}
		return Add( InKey, InValue );
	}
	INT Remove( typename TTypeInfo<TK>::ConstInitType InKey )
	{
		guardSlow(TMapBase::Remove);
		AllocHash();
		INT Count=0;
		for( INT i=Pairs.Num()-1; i>=0; i-- )
			if( Pairs(i).Key==InKey )
				{Pairs.Remove(i); Count++;}
		if( Count )
			Relax();
		return Count;
		unguardSlow;
	}
	TI* Find( const TK& Key )
	{
		guardSlow(TMapBase::Find);
		AllocHash();
		
		// scion sz - changed MOVETOTOP_PCT from float to an approximate 1/int 
		// in order to get rid of the implicit ftol
		INT MoveCount		= Max<INT>((Pairs.Num() / HashCount) / MOVETOTOP_DIVISOR, 3);
		INT StartHashIdx	= (GetTypeHash(Key) & (HashCount-1));
		INT PrevFindIdx		= -1;
		INT FindCount		=  0;
		for( INT i= Hash[StartHashIdx]; i!=INDEX_NONE; i=Pairs(i).HashNext )
		{
			if( Pairs(i).Key==Key )
			{
				if( PrevFindIdx >= 0 && FindCount >= MoveCount )
				{
					Pairs(PrevFindIdx).HashNext	= Pairs(i).HashNext;
					Pairs(i).HashNext			= Hash[StartHashIdx];
					Hash[StartHashIdx]			= i;
				}

				return &Pairs(i).Value;
			}	

			PrevFindIdx = i;
			FindCount++;
		}

		return NULL;
		unguardSlow;
	}
	TI FindRef( const TK& Key )
	{
		guardSlow(TMapBase::Find);
		if(Hash)
		{
			// scion sz - changed MOVETOTOP_PCT from float to an approximate 1/int 
			// in order to get rid of the implicit ftol
			INT MoveCount		= Max<INT>((Pairs.Num() / HashCount) / MOVETOTOP_DIVISOR, 3);
			INT StartHashIdx	= (GetTypeHash(Key) & (HashCount-1));
			INT PrevFindIdx		= -1;
			INT FindCount		=  0;
			for( INT i= Hash[StartHashIdx]; i!=INDEX_NONE; i=Pairs(i).HashNext )
			{
				if( Pairs(i).Key==Key )
				{
					if( PrevFindIdx >= 0 && FindCount >= MoveCount )
					{
						Pairs(PrevFindIdx).HashNext	= Pairs(i).HashNext;
						Pairs(i).HashNext			= Hash[StartHashIdx];
						Hash[StartHashIdx]			= i;
					}

					return Pairs(i).Value;
				}	

				PrevFindIdx = i;
				FindCount++;
			}
		}
		return (TI) NULL;
		unguardSlow;
	}
	const TI* Find( const TK& Key ) const
	{
		guardSlow(TMapBase::Find);
		if(Hash)
		{
			for( INT i=Hash[(GetTypeHash(Key) & (HashCount-1))]; i!=INDEX_NONE; i=Pairs(i).HashNext )
			{
				if( Pairs(i).Key==Key )
				{
					return &Pairs(i).Value;
				}	
			}
		}
		return NULL;
		unguardSlow;
	}
	friend FArchive& operator<<( FArchive& Ar, TMapBase& M )
	{
		guardSlow(TMapBase<<);
		Ar << M.Pairs;
		if( Ar.IsLoading() )
			M.Rehash();
		return Ar;
		unguardSlow;
	}
	void Dump( FOutputDevice& Ar )
	{
		guard(TMapBase::Dump);
		AllocHash();
		Ar.Logf( TEXT("TMapBase: %i items, %i hash slots"), Pairs.Num(), HashCount );
		for( INT i=0; i<HashCount; i++ )
		{
			INT c=0;
			for( INT j=Hash[i]; j!=INDEX_NONE; j=Pairs(j).HashNext )
			{
				c++;
			}
			Ar.Logf( TEXT("   Hash[%i] = %i"), i, c );
		}
		unguard;
	}
	class TIterator
	{
	public:
		TIterator( TMapBase& InMap ) : Map( InMap ), Pairs( InMap.Pairs ), Index( 0 ), Removed(0) {}
		~TIterator()               { if( Removed ) Map.Relax(); }
		void operator++()          { ++Index; }
		void RemoveCurrent()       { Pairs.Remove(Index--); Removed++; }
		operator UBOOL() const     { return Index<Pairs.Num(); }
		TK& Key() const            { return Pairs(Index).Key; }
		TI& Value() const          { return Pairs(Index).Value; }
	private:
		TMapBase& Map;
		TArray<TPair>& Pairs;
		INT Index;
		INT Removed;
	};
	friend class TIterator;
};

//scion jg -- Added the third parameter so I could control the minimum hash size
template< class TK, class TI,INT MinHashSize > class TMap : public TMapBase<TK,TI,MinHashSize>
{
public:
	TMap& operator=( const TMap& Other )
	{
		TMapBase<TK,TI,MinHashSize>::operator=( Other );
		return *this;
	}

	// scion sz
#if __MWERKS__
	TI& Add( struct TTypeInfo<TK>::ConstInitType InKey, TTypeInfo<TI>::ConstInitType InValue )
#else
	TI& Add( typename TTypeInfo<TK>::ConstInitType InKey, typename TTypeInfo<TI>::ConstInitType InValue )
#endif
	{
		return TMapBase<TK,TI,MinHashSize>::Add( InKey, InValue );
	}

	int Num()
	{
		guardSlow(TMap::Num);
		return Pairs.Num();
		unguardSlow;
	}
};
//scion jg -- Added the third parameter so I could control the minimum hash size
template< class TK, class TI, INT MinHashSize > class TMultiMap : public TMapBase<TK,TI,MinHashSize>
{
public:
	TMultiMap& operator=( const TMultiMap& Other )
	{
		TMapBase<TK,TI>::operator=( Other );
		return *this;
	}
	void MultiFind( const TK& Key, TArray<TI>& Values )
	{
		guardSlow(TMap::MultiFind);
		if(Hash)
		{
			for( INT i=Hash[(GetTypeHash(Key) & (HashCount-1))]; i!=INDEX_NONE; i=Pairs(i).HashNext )
				if( Pairs(i).Key==Key )
					new(Values)TI(Pairs(i).Value);
		}
		unguardSlow;
	}
#if __MWERKS__
	TI& Add( struct TTypeInfo<TK>::ConstInitType InKey, TTypeInfo<TI>::ConstInitType InValue )
#else
	TI& Add( typename TTypeInfo<TK>::ConstInitType InKey, typename TTypeInfo<TI>::ConstInitType InValue )
#endif
	{
		return TMapBase<TK,TI>::Add( InKey, InValue );
	}
#if __MWERKS__
	TI& AddUnique( struct TTypeInfo<TK>::ConstInitType InKey, struct TTypeInfo<TI>::ConstInitType InValue )
#else
	TI& AddUnique( typename TTypeInfo<TK>::ConstInitType InKey, typename TTypeInfo<TI>::ConstInitType InValue )
#endif
	{
		AllocHash();
		for( INT i=Hash[(GetTypeHash(InKey) & (HashCount-1))]; i!=INDEX_NONE; i=Pairs(i).HashNext )
			if( Pairs(i).Key==InKey && Pairs(i).Value==InValue )
				return Pairs(i).Value;
		return Add( InKey, InValue );
	}
#if __MWERKS__
	INT RemovePair( struct TTypeInfo<TK>::ConstInitType InKey, struct TTypeInfo<TI>::ConstInitType InValue )
#else
	INT RemovePair( typename TTypeInfo<TK>::ConstInitType InKey, typename TTypeInfo<TI>::ConstInitType InValue )
#endif
	{
		guardSlow(TMap::Remove);
		INT Count=0;
		for( INT i=Pairs.Num()-1; i>=0; i-- )
			if( Pairs(i).Key==InKey && Pairs(i).Value==InValue )
				{Pairs.Remove(i); Count++;}
		if( Count )
			Relax();
		return Count;
		unguardSlow;
	}
	TI* FindPair( const TK& Key, const TK& Value )
	{
		guardSlow(TMap::Find);
		if ( Hash )
		{
			for( INT i=Hash[(GetTypeHash(Key) & (HashCount-1))]; i!=INDEX_NONE; i=Pairs(i).HashNext )
				if( Pairs(i).Key==Key && Pairs(i).Value==Value )
					return &Pairs(i).Value;
		}
		return NULL;
		unguardSlow;
	}
};

/*----------------------------------------------------------------------------
	Sorting template.
----------------------------------------------------------------------------*/

//
// Sort elements. The sort is unstable, meaning that the ordering of equal
// items is not necessarily preserved.
//
template<class T> struct TStack
{
	T* Min;
	T* Max;
};
template<class T> void Sort( T* First, INT Num )
{
	guard(Sort);
	if( Num<2 )
		return;
	TStack<T> RecursionStack[32]={{First,First+Num-1}}, Current, Inner;
	for( TStack<T>* StackTop=RecursionStack; StackTop>=RecursionStack; --StackTop )
	{
		Current = *StackTop;
	Loop:
		INT Count = Current.Max - Current.Min + 1;
		if( Count <= 8 )
		{
			// Use simple bubble-sort.
			while( Current.Max > Current.Min )
			{
				T *Max, *Item;
				for( Max=Current.Min, Item=Current.Min+1; Item<=Current.Max; Item++ )
					if( Compare(*Item, *Max) > 0 )
						Max = Item;
				Exchange( *Max, *Current.Max-- );
			}
		}
		else
		{
			// Grab middle element so sort doesn't exhibit worst-cast behaviour with presorted lists.
			Exchange( Current.Min[Count/2], Current.Min[0] );

			// Divide list into two halves, one with items <=Current.Min, the other with items >Current.Max.
			Inner.Min = Current.Min;
			Inner.Max = Current.Max+1;
			for( ; ; )
			{
				while( ++Inner.Min<=Current.Max && Compare(*Inner.Min, *Current.Min) <= 0 );
				while( --Inner.Max> Current.Min && Compare(*Inner.Max, *Current.Min) >= 0 );
				if( Inner.Min>Inner.Max )
					break;
				Exchange( *Inner.Min, *Inner.Max );
			}
			Exchange( *Current.Min, *Inner.Max );

			// Save big half and recurse with small half.
			if( Inner.Max-1-Current.Min >= Current.Max-Inner.Min )
			{
				if( Current.Min+1 < Inner.Max )
				{
					StackTop->Min = Current.Min;
					StackTop->Max = Inner.Max - 1;
					StackTop++;
				}
				if( Current.Max>Inner.Min )
				{
					Current.Min = Inner.Min;
					goto Loop;
				}
			}
			else
			{
				if( Current.Max>Inner.Min )
				{
					StackTop->Min = Inner  .Min;
					StackTop->Max = Current.Max;
					StackTop++;
				}
				if( Current.Min+1<Inner.Max )
				{
					Current.Max = Inner.Max - 1;
					goto Loop;
				}
			}
		}
	}
	unguard;
}

/*----------------------------------------------------------------------------
	TDoubleLinkedList.
----------------------------------------------------------------------------*/

//
// Simple double-linked list template.
//
template< class T > class TDoubleLinkedList : public T
{
public:
	TDoubleLinkedList* Next;
	TDoubleLinkedList** PrevLink;
	void Unlink()
	{
		if( Next )
			Next->PrevLink = PrevLink;
		*PrevLink = Next;
	}
	void Link( TDoubleLinkedList*& Before )
	{
		if( Before )
			Before->PrevLink = &Next;
		Next     = Before;
		PrevLink = &Before;
		Before   = this;
	}
};

/*----------------------------------------------------------------------------
	TList.
----------------------------------------------------------------------------*/

//
// Simple single-linked list template.
//
template <class ElementType> class TList
{
public:

	ElementType			Element;
	TList<ElementType>*	Next;

	// Constructor.

	TList(ElementType InElement,TList<ElementType>* InNext = NULL)
	{
		Element = InElement;
		Next = InNext;
	}
};

/*----------------------------------------------------------------------------
	FRainbowPtr.
----------------------------------------------------------------------------*/

//
// A union of pointers of all base types.
//
union CORE_API FRainbowPtr
{
	// All pointers.
	void*  PtrVOID;
	BYTE*  PtrBYTE;
	_WORD* PtrWORD;
	DWORD* PtrDWORD;
	QWORD* PtrQWORD;
	FLOAT* PtrFLOAT;

	// Conversion constructors.
	FRainbowPtr() {}
	FRainbowPtr( void* Ptr ) : PtrVOID(Ptr) {};
};

//scion jg -- Template for autodeleting a pointer when a class goes out of scope
template<class T> class TScopePointer
{
	T* pPointer;

	// Don't allow this
	TScopePointer(TScopePointer&) {}
	operator=(T*) {}
	// Deletes the pointer
	FORCEINLINE void Release(void)
	{
		if (pPointer)
		{
			delete pPointer;
			pPointer = NULL;
		}
	}

public:
	// Construct a new scope pointer
	TScopePointer(T* pNew) : pPointer(pNew) {}
	// Destroy the pointer if valid
	~TScopePointer()
	{
		Release();
	}
	// Operators for getting at the data member
	FORCEINLINE operator T* () { return pPointer; }
	FORCEINLINE operator T** () { return &pPointer; }
	FORCEINLINE operator&()  { return &pPointer; }
	// Accessors
	FORCEINLINE T* Get(void) { return pPointer; }
	FORCEINLINE void Set(T* pNew,UBOOL bRelease = 0)
	{
		// Clean up before assignment if requested
		if (bRelease == 1)
		{
			Release();
		}
		pPointer = pNew;
	}
};

// scion sz -
// useful class for building trees
template<class T>
class TTree
{
	T Item;
	TTree<T>* Parent;
	TArray<TTree<T>*> Children;

public:
	TTree( T& InItem, TTree<T>* InParent=NULL )
	{
		Item = InItem;
		Parent = InParent;
		if( Parent )
		{
			Parent->Children.AddItem( this );
		}
	}

	~TTree()
	{
		for( INT i=0; i<Children.Num(); i++ )
		{
			TTree<T>* pChild = Children(i);
			if( pChild )
				delete pChild;
		}
		Children.Empty();
	}

	void AddChild( TTree<T>* InChild )
	{
		Children.AddItem( InChild );
	}

	void AddChildItem( T& InItem )
	{	
		TTree<T>* NewChild = new TTree<T>( InItem );
        Children.AddItem( NewChild );
	}

	TTree<T>* GetChild( INT index )
	{
		check( index < Children.Num() );
		return Children(index);
	}

	T& GetChildItem( INT index )
	{
		check( index < Children.Num() );
		return Children(index)->Item;
	}

	T& GetItem()
	{
		return Item;
	}

	TTree<T*> GetParent()
	{
		return Parent;
	}

	INT NumChildren()
	{
		return Children.Num();
	}

};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This class is a non-dynamically allocating hash map. It allows you to
 * presize the memory usage and hash table for optimal search control.
 *
 * ============================================================================
 */
template <class KEY, class TYPE, INT MaxItems, INT HashSize> class TFixedSizeMap
{
	// Templated item holder class
	template <class IKEY, class ITEM> class _FixedSizedMapItem
	{
	public:
		// The item being held
		ITEM Item;
		// The key that was used to place this item in the hash
		IKEY Key;
		// The next item in the chain
		_FixedSizedMapItem* Next;
	};
	// So I can be lazy with my typing
	typedef _FixedSizedMapItem<KEY,TYPE> FFixedSizedMapItem;

	// The next item to use from our FFixedSizedMapItem pool
	INT CurrItem;
	// Pool of key/value pairs so there are no dynamic allocations
	FFixedSizedMapItem Items[MaxItems];
	// Hash table
	FFixedSizedMapItem* Hash[HashSize];

public:
	FORCEINLINE TFixedSizeMap()
	{
		Empty();
	}

	// Searches the hash for an existing item
	FORCEINLINE TYPE Find(KEY key)
	{
		// Find the slot in the hash this key would occupy
		INT HashIndex = GetTypeHash(key) % HashSize;
		// Get the item at that location
		FFixedSizedMapItem* pItem = Hash[HashIndex];
		// Search until a match is found or NULL is found
		while (pItem != NULL && pItem->Key != key)
		{
			pItem = pItem->Next;
		}
		return pItem ? pItem->Item : NULL;
	}

	// Adds an item to our hash
	FORCEINLINE void Add(KEY key,TYPE type)
	{
		// Make sure we don't overrun our array
		checkSlow(CurrItem < MaxItems);
		// Duplicates are not handled
		checkSlow(Find(key) == NULL);
		// Get a new item from our pool and update the next item to be used
		FFixedSizedMapItem* pNewItem = &Items[CurrItem++];
		// Set the key/value pairs
		pNewItem->Key = key;
		pNewItem->Item = type;
		// Find where in the hash this item will live
		INT HashIndex = GetTypeHash(key) % HashSize;
		// Determine if someone is already there
		FFixedSizedMapItem* pItem = Hash[HashIndex];
		// Link the existing item in. NULL here is fine.
		pNewItem->Next = pItem;
		// Now place our new item in the hash here
		Hash[HashIndex] = pNewItem;
	}

	// Clears the map for reuse
	FORCEINLINE void Empty(void)
	{
		appMemzero(Hash,sizeof(FFixedSizedMapItem*) * HashSize);
		appMemzero(Items,sizeof(FFixedSizedMapItem) * MaxItems);
		CurrItem = 0;
	}
};

//scion jg -- UMA/D3D version of a templated array. It allocates its memory in
// contiguous space so that it can be accessed by the GPU. If the CPU
// needs access to this data then it should change the page access type
// so that the CPU can cache it. If not, access will be slow.
template<class TYPE, DWORD dwAlign = 4> class TUmaArray
{
	// Holds the data as an array
	TYPE* pTypeArray;
	// The number of items in the array. Note fixed size.
	INT Count;
	// Whether this block came from a XPR file or not
	UBOOL bIsInXPR;

public:
	// Simple constructor that zeros the members
	TUmaArray()
	{
		pTypeArray = NULL;
		Count = 0;
	}

	~TUmaArray()
	{
#if defined(XBOX_ASSET_FORMAT) && defined(_XBOX)
		// If this was from an XPR don't delete since it will get freed
		// when the block is unloaded
		if (pTypeArray != NULL && bIsInXPR == 0)
#else
		if (pTypeArray != NULL)
#endif
		{
			// Memory came from contiguous so return to there
			appContiguousFree(pTypeArray);
		}
		// Zero our members
		pTypeArray = NULL;
		Count = 0;
	}

	/**
	 * Tells this array that it's data will come from an XPR file
	 */
	FORCEINLINE void MarkAsInXPR(void)
	{
		guard(TUmaArray::MarkAsInXPR);
		bIsInXPR = 1;
		unguard;
	}

	/**
	 * Indicates if this array should be loaded from an XPR file or not
	 */
	FORCEINLINE UBOOL IsInXPR(void)
	{
		guard(TUmaArray::IsInXPR);
		return bIsInXPR;
		unguard;
	}

	/**
	 * Sets the count and pointer data directly. Can only be used for data
	 * that comes from an XPR file.
	 *
	 * @param InCount the new count value to set
	 * @param InData the new data pointer to assign
	 */
	FORCEINLINE void FixUpArray(INT InCount,void* InData)
	{
		guard(TUmaArray::FixUpArray);
#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
		// Don't ever call this on an array that is not in an XPR file
		check(bIsInXPR);
#endif
		// Now set the new data
		Count = InCount;
		pTypeArray = (TYPE*)InData;
		unguard;
	}

	// Non const operator to allow access to members of the array
	FORCEINLINE TYPE& operator()(INT Index)
	{
		guard(TUmaArray::operator());
		checkSlow(Index >= 0);
		checkSlow(Index < Count);
		// Get the item at the requested index
		return pTypeArray[Index];
		unguard;
	}

	// Operator to allow access to members of the array
	FORCEINLINE const TYPE& operator()(INT Index) const
	{
		guard(TUmaArray::operator());
		checkSlow(Index >= 0);
		checkSlow(Index < Count);
		// Get the item at the requested index
		return pTypeArray[Index];
		unguard;
	}

	// Operator to allow access to members of the array
	FORCEINLINE const INT Num(void) const
	{
		guard(TUmaArray::Num());
		return Count;
		unguard;
	}

	// Can't add to a contiguous array
	FORCEINLINE INT Add(INT NumItems = 1)
	{
		guard(TUmaArray::Add());
		check(0);
		return NumItems;
		unguard;
	}

	// Can't add to a contiguous array
	FORCEINLINE INT AddZeroed(INT NumItems = 1)
	{
		guard(TUmaArray::AddZeroed());
		check(0);
		return NumItems;
		unguard;
	}

	FORCEINLINE void CountBytes( FArchive& Ar )
	{
		guardSlow(TUmaArray::CountBytes);
		Ar.CountBytes( sizeof(TYPE) * Count,sizeof(TYPE) * Count, sizeof(TYPE) * Count );
		unguardSlow;
	}

	// Serialization function
	friend FArchive& operator<<( FArchive& Ar, TUmaArray& A )
	{
		guard(TUmaArray::operator<<);
		// We only support loading
		if (Ar.IsLoading())
		{
#if defined(XBOX_ASSET_FORMAT)
			A.bIsInXPR = 0;
			// Only load this if we have the converted format
			if (Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
			{
				Ar << A.bIsInXPR;
			}
			A.Count = 0;
			// Only allocate and serialize if it is NOT in a XPR
			if (A.bIsInXPR == 0)
			{
				// Load array size
				Ar << AR_INDEX(A.Count);
				if (A.Count > 0)
				{
					// Allocate the chunk of contiguous memory
					A.pTypeArray = (TYPE*)appContiguousMalloc(sizeof(TYPE) * A.Count,
						dwAlign);
					// Serialize in each item into the preallocated chunk of RAM
					for (INT ItemIndex = 0; ItemIndex < A.Count; ItemIndex++)
					{
						Ar << A(ItemIndex);
					}
					// Assume we want fast CPU access
					appMemCPUFastAccess(A.pTypeArray,sizeof(TYPE) * A.Count);
				}
			}
#else
			// Load array size
			Ar << AR_INDEX(A.Count);
			if (A.Count > 0)
			{
				// Allocate the chunk of contiguous memory
				A.pTypeArray = (TYPE*)appContiguousMalloc(sizeof(TYPE) * A.Count,
					dwAlign);
				// Serialize in each item into the preallocated chunk of RAM
				for (INT ItemIndex = 0; ItemIndex < A.Count; ItemIndex++)
				{
					Ar << A(ItemIndex);
				}
				// Assume we want fast CPU access
				appMemCPUFastAccess(A.pTypeArray,sizeof(TYPE) * A.Count);
			}
#endif
		}
		else if (Ar.IsSaving())
		{
#if defined(XBOX_ASSET_FORMAT)
			// Write out whether there will be data in this file or not
			Ar << A.bIsInXPR;
			// Only serialize if it is NOT in a XPR
			if (A.bIsInXPR == 0)
			{
				// Write the array size
				Ar << AR_INDEX(A.Count);
				// Serialize each item to disk
				for (INT ItemIndex = 0; ItemIndex < A.Count; ItemIndex++)
				{
					Ar << A(ItemIndex);
				}
			}
#else
			// No saving of TUmaArray objects
			checkSlow(0);
#endif
		}
		else
		{
			// Tell the archive the size serialized
			A.CountBytes( Ar );			
			for( INT i=0; i < A.Count; ++i )
			{				
				Ar << A(i);
			}
		}		
		
		return Ar;
		unguard;
	}
};

// Prototype for finding bulk exported TUmaArray data
UBOOL GetTUmaArrayFromXPR(const TCHAR* ArrayName,INT& OutCount,void*& OutData);

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This templated class automatically pools instances of the list nodes to
 * prevent lots of small allocates/deletes. It allocates in blocks of nodes
 * (configurable) to keep the small allocations down. You must use the factory
 * methods only.
 *
 * ============================================================================
 */
template <class TYPE,DWORD BlockSize = 256> class TPooledList
{
	// The singleton instance for memory book keeping
	template <class TYPE> struct TPoolInstance
	{
		// Maintains the list of block allocations. These blocks are freed not
		// the individual items in the block
		template <class TYPE> struct TBlockAllocationList
		{
			// The start of memory contained in this block
			TYPE* pStart;
			// The next block in the chain
			TBlockAllocationList* pNext;
			// Simple constructor
			TBlockAllocationList<TYPE>(TYPE* Start,
				TBlockAllocationList<TYPE>* Next)
			{
				pStart = Start;
				pNext = Next;
			}
		};
		// The list of blocks that have been allocated
		TBlockAllocationList<TPooledList<TYPE> >* BlockList;
		// This is the list of previously allocated list objects. Reuse rather
		// than allocating/freeing them repeatedly to prevent fragmentation
		TPooledList<TYPE>* ListPool;
#ifdef _DEBUG
		// Track usage stats
		DWORD NumBlocksUsed;
		DWORD NumNodesUsed;
		DWORD MaxNodesUsed;
#endif
		// Constructor that zeros
		TPoolInstance(void)
		{
			ListPool = NULL;
			BlockList = NULL;
#ifdef _DEBUG
			NumBlocksUsed = NumNodesUsed = MaxNodesUsed = 0;
#endif
		}
	};
	// The single pool instance
	static TPoolInstance<TYPE> PoolInstance;

	// Hide ctors
	TPooledList(TYPE Type,TPooledList<TYPE>* pNext);
	TPooledList(void);
	// Hide delete operator
	void operator delete(void* pVoid,size_t Size);

	/* scion ======================================================================
	 * TPooledList<TYPE>::AllocateBlock
	 * Author: jg
	 * 
	 * Allocates a block of TPooledList<TYPE> nodes, links them together and sets
	 * the ListPool to point to this block
	 *
	 * ============================================================================
	 */
	static void AllocateBlock(void)
	{
		guard(TPooledList<>::AllocateBlock);
		// Only allocate blocks when we are empty
		check(PoolInstance.ListPool == NULL);
		// Allocate our block of list nodes
		TPooledList<TYPE>* pMem =
			(TPooledList<TYPE>*)appMalloc(sizeof(TPooledList<TYPE>) * BlockSize);
		// Null out all pointers
		appMemzero(pMem,sizeof(TPooledList<TYPE>) * BlockSize);
		// For walking the new list of items setting up their next pointers
		TPooledList<TYPE>* pNext = pMem;
		// Move through all the newly allocated pointers and set their next pointers
		for (INT Count = 0; Count < BlockSize - 1; Count++)
		{
			TPooledList<TYPE>* pLast = pNext++;
			pLast->Next = pNext;
		}
		// Create a new block list entry and place at the front of the list
		PoolInstance.BlockList = NEW
			TPoolInstance<TYPE>::TBlockAllocationList<TPooledList<TYPE> > (pMem,
			PoolInstance.BlockList);
		// Now set the list pool as the newly allocated block
		PoolInstance.ListPool = pMem;
#ifdef _DEBUG
		PoolInstance.NumBlocksUsed++;
#endif
		unguard;
	}

public:
	// Public members (should private but that would require too much refactoring)
	TYPE Element;
	TPooledList<TYPE>* Next;

	/* scion ======================================================================
	 * TPooledList<TYPE>::GetInstance
	 * Author: jg
	 * 
	 * Checks the list pool for an available object and creates a new one if needed
	 *
	 * input:	Type -- The item to assign to element
	 *			pNext -- The next item in the chain
	 *
	 * ============================================================================
	 */
	static inline TPooledList<TYPE>* GetInstance(TYPE& Type,TPooledList<TYPE>* pNext)
	{
		guard(TPooledList<>::GetInstance);
		// Give it the first node in the list
		TPooledList<TYPE>* pListItem = GetInstance(pNext);
		// Assign the new value to it
		pListItem->Element = Type;
		// Return the newly constructed node
		return pListItem;
		unguard;
	}

	/* scion ======================================================================
	 * TPooledList<TYPE>::GetInstance
	 * Author: jg
	 * 
	 * Checks the list pool for an available object and creates a new one if needed
	 * This version assumes that the TYPE has a default constructor to set the values
	 *
	 * input:	pNext -- The next item in the chain
	 *
	 * ============================================================================
	 */
	static inline TPooledList<TYPE>* GetInstance(TPooledList<TYPE>* pNext)
	{
		guard(TPooledList<>::GetInstance);
		// If there are no items available allocate a new block
		if (PoolInstance.ListPool == NULL)
		{
			// We need a new block of items to pull from
			AllocateBlock();
		}
		check(PoolInstance.ListPool);
		// Give it the first node in the list
		TPooledList<TYPE>* pListItem = PoolInstance.ListPool;
		// Snip off this node
		PoolInstance.ListPool = PoolInstance.ListPool->Next;
		// Assign the next that was specified
		pListItem->Next = pNext;
#ifdef _DEBUG
		PoolInstance.NumNodesUsed++;
		PoolInstance.MaxNodesUsed = Max(PoolInstance.MaxNodesUsed,
			PoolInstance.NumNodesUsed);
#endif
		// Return the newly constructed node
		return pListItem;
		unguard;
	}

	/* scion ======================================================================
	 * TPooledList<TYPE>::ReturnInstance
	 * Author: jg
	 * 
	 * Takes the TPooledList<TYPE>* object and inserts it into the pool.
	 *
	 * input:	pListItem -- The list item to return to the pool
	 *
	 * ============================================================================
	 */
	static inline void ReturnInstance(TPooledList<TYPE>* pListItem)
	{
		guard(TPooledList<TYPE>::ReturnInstance);
		check(pListItem);
		// Insert this at the front of the list
		pListItem->Next = PoolInstance.ListPool;
		PoolInstance.ListPool = pListItem;
#ifdef _DEBUG
		PoolInstance.NumNodesUsed--;
#endif
		unguard;
	}

	/* scion ======================================================================
	 * TPooledList<TYPE>::FreePool
	 * Author: jg
	 * 
	 * Releases all memory used by the pool
	 *
	 * ============================================================================
	 */
	static void FreePool(void)
	{
		guard(TPooledList<TYPE>::FreePool);
		// Mark the list pool as being empty
		PoolInstance.ListPool = NULL;
		// Walk the allocated block chain and free them. This is really bad if
		// there are nodes still in use.
		while (PoolInstance.BlockList != NULL)
		{
			// Snip off the first block in the chain
			TPoolInstance<TYPE>::TBlockAllocationList<TPooledList<TYPE> >* pDel =
				PoolInstance.BlockList;
			PoolInstance.BlockList = PoolInstance.BlockList->pNext;
			// Free the memory contained in that block
			appFree(pDel->pStart);
			// Now delete the block
			delete pDel;
		}
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
