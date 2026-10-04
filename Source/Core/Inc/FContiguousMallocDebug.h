#ifndef _FCONTIGUOUSMALLOC_DEBUG_H
#define _FCONTIGUOUSMALLOC_DEBUG_H

#include "FContiguousMalloc.h"

class CORE_API FContiguousMallocDebug : public FAlignedMalloc
{
	struct FDebugInfo 
	{
		enum { MAX_FILENAME_SIZE = 128 };

		void*		Address;
		DWORD		Size;
		TCHAR		FileName[MAX_FILENAME_SIZE];
		INT			Line;
		DWORD		Tag;
	};
	TArray<FDebugInfo>	DebugInfoList;

	FContiguousMalloc*	ContigMalloc;

public:
	FContiguousMallocDebug() :
		ContigMalloc(NULL) 
	{
	}

	FContiguousMallocDebug( FContiguousMalloc* InContigMalloc ) :
		ContigMalloc( InContigMalloc )
	{
		check( ContigMalloc );
	}

	virtual ~FContiguousMallocDebug()
	{
		Exit();
	}

	// FAlignedMalloc interface
	virtual void* AlignedMalloc(DWORD Size,DWORD Align, const ANSICHAR* File = __FILE__, INT Line = __LINE__ );

	// FMalloc interface
	virtual void* Malloc( DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  );
	virtual void* Realloc( void* Original, DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  );
	virtual void Free( void* Original );
	virtual void DumpAllocs();
	virtual void HeapCheck();
	virtual void Init();
	virtual void Exit();

	// debug info
	void DumpTaggedAllocs( DWORD Tags[], INT NumTags=1 );

};

#endif