#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#undef _WIN32_IE
#define _WIN32_IE 0x0400
#endif

#include <atlbase.h>
// Include the interface definition
#include "..\..\FNameDebugCOM\FNameDebugCOM.h"
#include "FNameDebug.h"

// This has to be moved out of the class definition to avoid errors that
// occur when Windows header files are included in Core.
CComPtr<IFNameDebug> g_pIFNameDebug;
FNameDebug g_FNameDebug;

//
// Initializes COM and creates an instance of the COM object
//
void FNameDebug::Init(void)
{
	// Only init once
	if (g_pIFNameDebug == NULL)
	{
		// Load the COM subsystem
		HRESULT hr = CoInitializeEx(NULL,COINIT_MULTITHREADED);
		if (SUCCEEDED(hr))
		{
			// Now we need to get a copy of the COM object for ourselves
			hr = g_pIFNameDebug.CoCreateInstance(CLSID_FNameDebug);
			if (FAILED(hr))
			{
				OutputDebugString(_T("Failed to create COM object"));
			}
		}
	}
}

//
// Adds the FName information to the debugging COM object
//
void FNameDebug::AddFName(int nID,unsigned short const * szString)
{
	// Only use if the COM object was created correctly
	if (g_pIFNameDebug != NULL)
	{
		// Add this FName to the COM object
		g_pIFNameDebug->put_FName((DWORD)nID,CComBSTR(szString));
	}
}

//
// Releases the COM object and the COM subsystem
//
void FNameDebug::Release(void)
{
	// Release the COM object
	g_pIFNameDebug.Release();
	// I am sure this is incredibly bad since COM is not shut down nicely
	// No idea why the kernel is deadlocking here. Not cool.
//	CoUninitialize();
}
// HACK to make linker happy
#pragma warning(disable: 4273)
int _CrtDbgReport(int,const char*,int,const char*,
	const char*,...) { return 0; }
