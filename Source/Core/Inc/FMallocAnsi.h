/*=============================================================================
	FMallocAnsi.h: ANSI memory allocator.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

//
// ANSI C memory allocator.
//
class FMallocAnsi : public FMalloc
{
public:
	// FMalloc interface.
	//scion jg -- Added the __FILE__ & __LINE__ vars for improved memory tracking
#ifndef FINAL_RELEASE
	void* Malloc( DWORD Size, const ANSICHAR* File = __FILE__, INT Line = __LINE__ )
#else
	void* Malloc( DWORD Size )
#endif
	{
		guard(FMallocAnsi::Malloc);
		check(Size>=0);
		void* Ptr = malloc( Size );
		check(Ptr);
		return Ptr;
		unguard;
	}
	//scion jg -- Added the __FILE__ & __LINE__ vars for improved memory tracking
#ifndef FINAL_RELEASE
	void* Realloc( void* Ptr, DWORD NewSize, const ANSICHAR* File = __FILE__, INT Line = __LINE__ )
#else
	void* Realloc( void* Ptr, DWORD NewSize )
#endif
	{
		guard(FMallocAnsi::Realloc);
		check(NewSize>=0);
		void* Result;
		if( Ptr && NewSize )
		{
			Result = realloc( Ptr, NewSize );
		}
		else if( NewSize )
		{
			Result = malloc( NewSize );
		}
		else
		{
			if( Ptr )
				free( Ptr );
			Result = NULL;
		}
		return Result;
		unguardf(( TEXT("%08X %i %s:%i"), (INT)(PTRINT)Ptr, NewSize, File, Line ));	// M13: the low half is enough for the message
	}
	void Free( void* Ptr )
	{
		guard(FMallocAnsi::Free);
		free( Ptr );
		unguard;
	}
	void DumpAllocs()
	{
		guard(FMallocAnsi::DumpAllocs);
		debugf( NAME_Exit, TEXT("Allocation checking disabled") );
		unguard;
	}
	void HeapCheck()
	{
		guard(FMallocAnsi::HeapCheck);
#if (defined _MSC_VER) && (!defined _XBOX || defined _DEBUG) && (!defined __GCN__)
		INT Result = _heapchk();
		check(Result!=_HEAPBADBEGIN);
		check(Result!=_HEAPBADNODE);
		check(Result!=_HEAPBADPTR);
		check(Result!=_HEAPEMPTY);
		check(Result==_HEAPOK);
#endif
		unguard;
	}
	void Init()
	{
		guard(FMallocAnsi::Init);
		unguard;
	}
	void Exit()
	{
		guard(FMallocAnsi::Exit);
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

