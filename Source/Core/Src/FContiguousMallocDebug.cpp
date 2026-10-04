#include "CorePrivate.h"
#include "FContiguousMallocDebug.h"

void* FContiguousMallocDebug::AlignedMalloc( DWORD Size,DWORD Align, const ANSICHAR* File, INT Line )
{
#ifdef FINAL_RELEASE
	void* Result = ContigMalloc->AlignedMalloc( Size, Align );
#else
	void* Result = ContigMalloc->AlignedMalloc( Size, Align, File, Line );
#endif

	DebugInfoList.Add();
	FDebugInfo& DebugInfo = DebugInfoList( DebugInfoList.Num()-1 );
	DebugInfo.Address = Result;
	DebugInfo.Size = Size;
	appStrncpy( DebugInfo.FileName, ANSI_TO_TCHAR(File), FDebugInfo::MAX_FILENAME_SIZE );
	DebugInfo.Line = Line;
	DebugInfo.Tag = GMemoryTag;

	return Result;
}

void* FContiguousMallocDebug::Malloc( DWORD Count, const ANSICHAR* File, INT Line )
{
	void* Result = AlignedMalloc( Count, 4, File, Line );
	return Result;
}

void* FContiguousMallocDebug::Realloc( void* Original, DWORD Count, const ANSICHAR* File, INT Line )
{
	check(0);
	return NULL;
}

void FContiguousMallocDebug::Free( void* Original )
{
	ContigMalloc->Free( Original );

	UBOOL Fount = 0;
	for( INT i=0; i < DebugInfoList.Num(); i++ )
	{
		if( DebugInfoList(i).Address == Original )
		{
			DebugInfoList.Remove(i);
			Fount = 1;
			break;
		}
	}
}

void FContiguousMallocDebug::DumpAllocs()
{
	debugf( NAME_DevSam, TEXT("==========================") );
	debugf( NAME_DevSam, TEXT("List of Contiguous Allocs:") );

	for( INT i=0; i < DebugInfoList.Num(); i++ )
	{
		FDebugInfo& DebugInfo = DebugInfoList(i);
		debugf( NAME_DevSam, TEXT("#%d Size=%d Tag=%d (%s line %d)"), i, DebugInfo.Size, DebugInfo.Tag, DebugInfo.FileName, DebugInfo.Line );
	}
	
    ContigMalloc->DumpAllocs();
}

void FContiguousMallocDebug::DumpTaggedAllocs( DWORD Tags[], INT NumTags )
{
	check( ARRAY_COUNT(Tags) >= NumTags );

	for( INT TagIdx=0; TagIdx < NumTags; TagIdx++ )
	{
		DWORD CheckTag = Tags[TagIdx];
		for( INT i=0; i < DebugInfoList.Num(); i++ )
		{
			FDebugInfo& DebugInfo = DebugInfoList(i);
			if( DebugInfo.Tag == CheckTag )
			{
				debugf( NAME_DevSam, TEXT("#%d Size=%d Tag=%d (%s line %d)"), i, DebugInfo.Size, DebugInfo.Tag, DebugInfo.FileName, DebugInfo.Line );
			}	
		}
	}	
}

void FContiguousMallocDebug::HeapCheck()
{
    ContigMalloc->HeapCheck();
}

void FContiguousMallocDebug::Init()
{
	check( ContigMalloc );
	ContigMalloc->Init();
}

void FContiguousMallocDebug::Exit()
{
	check( ContigMalloc );
	ContigMalloc->Exit();
}