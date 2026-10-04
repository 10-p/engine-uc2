/// 
/// Copyright © 2002 Epic Games. All right reserved.
/// 
/// 
/// <author>Joe Graf</author>
/// 

#include "stdafx.h"
#include <stdio.h>
#include <atlbase.h>
#include "FNameDebugCOM.h"

// Bogus DllMain that tell the kernel that we loaded ok
BOOL APIENTRY DllMain(HANDLE,DWORD,LPVOID)
{
    return TRUE;
}


// Commonly used methods follow


//
// Reads an FArray object into the specified chunk of memory
//
HRESULT ReadFArray(DEBUGHELPER* pHelper,DWORD dwAddress,FArray* pArray)
{
	DWORD dwGot = 0;
	return pHelper->ReadDebuggeeMemory(pHelper,dwAddress,sizeof(FArray),pArray,
		&dwGot);
}

//
// Reads an FName from the external COM object
//
HRESULT GetFName(DWORD dwIndex,BSTR* pbstrOut)
{
	CComPtr<IFNameDebug> pFNameDebug = NULL;
	// Create an instance of the COM object
	HRESULT hr = pFNameDebug.CoCreateInstance(CLSID_FNameDebug,NULL,
		CLSCTX_LOCAL_SERVER);
	if (SUCCEEDED(hr))
	{
		// Read the string for this ID
		hr = pFNameDebug->get_FName(dwIndex,pbstrOut);
	}
	return hr;
}

// Debugger hooks follow

//
// This method copies the contents of the FString into the designated buffer
//
extern "C" ADDIN_API HRESULT WINAPI DebugHelpFString(DWORD dwAddress,
	DEBUGHELPER* pHelper,int,BOOL,char* pResult,size_t Size,DWORD)
{
	USES_CONVERSION;
	HRESULT hr = E_FAIL;
	DWORD dwGot = 0;
	// Clear the outgoing value
	ZeroMemory(pResult,Size);
	// Receives the local copy of the data
	FString string;
	// Read the FString into our local memory space
	hr = pHelper->ReadDebuggeeMemory(pHelper,dwAddress,sizeof(FString),
		&string,&dwGot);
	if (SUCCEEDED(hr))
	{
		// Check to see if the local data section is valid
		if (string.Data != NULL)
		{
			WCHAR* szBuff = new WCHAR[string.ArrayNum + 1];
			// Now try to read the chunk of memory that's held by Data
			hr = pHelper->ReadDebuggeeMemory(pHelper,(DWORD)string.Data,
				sizeof(WCHAR) * string.ArrayNum,szBuff,&dwGot);
			if (SUCCEEDED(hr))
			{
				// Terminate the string
				szBuff[dwGot] = L'\0';
				// Convert the string over
				char* szString = W2A(szBuff);
				// Copy the converted string into the out buffer. Or at least as
				// much as it will hold
				strncpy(pResult,szString,min(Size - 1,strlen(szString)));
			}
			delete [] szBuff;
		}
		else
		{
			// Just copy in the value "Null"
			strcpy(pResult,"NULL");
		}
	}
	return hr;
}

//
// This method dereferences an FName's index value to find the FNameEntry
// that it corresponds to. It then copies the FNameEntry->Name data into
// the designated output buffer.
//
extern "C" ADDIN_API HRESULT WINAPI DebugHelpFName(DWORD dwAddress,
	DEBUGHELPER* pHelper,int nBase,BOOL,char* pResult,size_t Size,DWORD)
{
	USES_CONVERSION;
	HRESULT hr = E_FAIL;
	DWORD dwGot = 0;
	// Clear the outgoing value
	ZeroMemory(pResult,Size);
	// Receives the local copy of the data
	FName name;
	// Read the FName into our local memory space
	hr = pHelper->ReadDebuggeeMemory(pHelper,dwAddress,sizeof(FName),
		&name,&dwGot);
	if (SUCCEEDED(hr))
	{
		CComBSTR bstrName;
		// Use common FName lookup method
		hr = GetFName(name.Index,&bstrName);
		if (SUCCEEDED(hr))
		{
			// Convert the string over
			char* szString = W2A(bstrName);
			// Now format this into a string
			_snprintf(pResult,Size - 1,nBase == 10 ?
				"Index=%d, Name=%s" : "Index=0x%X, Name=%s",name.Index,
				szString);
		}
	}
	return hr;
}

//
// This method converts an FArray into a string displayable in the debugger
//
extern "C" ADDIN_API HRESULT WINAPI DebugHelpFArray(DWORD dwAddress,
	DEBUGHELPER* pHelper,int nBase,BOOL,char* pResult,size_t Size,DWORD)
{
	DWORD dwGot = 0;
	// Clear the outgoing value
	ZeroMemory(pResult,Size);
	// Receives the local copy of the data
	FArray Array;
	// Read the FArray into our local memory space
	HRESULT hr = ReadFArray(pHelper,dwAddress,&Array);
	if (SUCCEEDED(hr))
	{
		// Now format this into a string
		_snprintf(pResult,Size - 1,nBase == 10 ?
			"Data=%d, ArrayNum=%d, ArrayMax=%d" :
			"Data=0x%08X, ArrayNum=%d, ArrayMax=%d",
			(DWORD)Array.Data,Array.ArrayNum,Array.ArrayMax);
	}
	return hr;
}

//
// This method is used to get the name of any UObject derived entity
//
extern "C" ADDIN_API HRESULT WINAPI DebugHelpUObject(DWORD dwAddress,
	DEBUGHELPER* pHelper,int,BOOL,char* pResult,size_t Size,DWORD)
{
	USES_CONVERSION;
	HRESULT hr = E_FAIL;
	DWORD dwGot = 0;
	// Clear the outgoing value
	ZeroMemory(pResult,Size);
	// Receives the local copy of the data
	FName name;
	// Read the FName into our local memory space
	hr = pHelper->ReadDebuggeeMemory(pHelper,dwAddress + UOBJECT_FNAME_OFFSET,
		sizeof(FName),&name,&dwGot);
	if (SUCCEEDED(hr))
	{
		CComBSTR bstrName;
		// Use common FName lookup method
		hr = GetFName(name.Index,&bstrName);
		if (SUCCEEDED(hr))
		{
			// Convert the string over
			char* szString = W2A(bstrName);
			// Copy the converted string into the out buffer. Or at least as
			// much as it will hold
			strncpy(pResult,szString,min(Size - 1,strlen(szString)));
		}
	}
	return hr;
}
