/// 
/// Copyright © 2002 Epic Games. All right reserved.
/// 
/// 
/// <author>Joe Graf</author>
/// 

#ifndef _CUSTOM_DEBUGGING_VIEW_H
#define _CUSTOM_DEBUGGING_VIEW_H

// Disable the DWORD void* cast problem
#pragma warning(disable:4311)

#define ADDIN_API __declspec(dllexport)
#define VSDOTNETVERSION 0x20000

// The structure that is passed into the debug helper method
typedef struct tagDEBUGHELPER
{
	// < 0x20000 is VS 6 > 0x20000 VS 7
	DWORD dwVersion;
	BOOL (WINAPI *ReadDebuggeeMemory)(struct tagDEBUGHELPER* pThis,DWORD dwAddr,
		DWORD dwWant,VOID* pWhere,DWORD* dwGot);
	// From here only when dwVersion >= 0x20000
	DWORDLONG (WINAPI *GetRealAddress)(struct tagDEBUGHELPER* pThis);
	BOOL (WINAPI *ReadDebuggeeMemoryEx)(struct tagDEBUGHELPER* pThis,
		DWORDLONG qwAddr,DWORD dwWant,VOID* pWhere,DWORD* dwGot);
	int (WINAPI *GetProcessorType)(struct tagDEBUGHELPER* pThis);
} DEBUGHELPER;

typedef HRESULT (WINAPI *CUSTOMVIEWER)(DWORD dwAddress,DEBUGHELPER* pHelper,
	int nBase,BOOL bUniStrings,char* pResult,size_t max,DWORD reserved);

#endif //_CUSTOM_DEBUGGING_VIEW_H