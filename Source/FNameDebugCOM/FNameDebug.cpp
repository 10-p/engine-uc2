// FNameDebug.cpp : Implementation of CFNameDebugCOMApp and DLL registration.

#include "stdafx.h"
#include "FNameDebugCOM.h"
#include "FNameDebug.h"
#include "CScopeLock.h"

CMapIdToString FNameDebug::m_mapIdToString;

/////////////////////////////////////////////////////////////////////////////
//

STDMETHODIMP FNameDebug::InterfaceSupportsErrorInfo(REFIID riid)
{
	static const IID* arr[] = 
	{
		&IID_IFNameDebug,
	};

	for (int i=0;i<sizeof(arr)/sizeof(arr[0]);i++)
	{
		if (InlineIsEqualGUID(*arr[i],riid))
			return S_OK;
	}
	return S_FALSE;
}

//
// Finds the string data for the ID passed in
//
STDMETHODIMP FNameDebug::get_FName(DWORD dwID, BSTR* pbstrOut)
{
	// Prevent multiple threads accessing simulateously
	CScopeLock sl(&m_csAccessSync);
	USES_CONVERSION;
	*pbstrOut = NULL;
	CString strName;
	// Look in the map for the name at this location
	if (m_mapIdToString.Lookup(dwID,strName) == 0)
	{
		strName = _T("Unknown FName Index");
	}
	// Return the string at this location
	ATLTRY(*pbstrOut = SysAllocString((LPCTSTR)strName));
	return *pbstrOut == NULL ? E_FAIL : S_OK;
}

//
// Adds an FName's information to the map
//
STDMETHODIMP FNameDebug::put_FName(DWORD dwID, BSTR bstrIn)
{
	// Prevent multiple threads accessing simulateously
	CScopeLock sl(&m_csAccessSync);
	// Update the map as needed
	m_mapIdToString.SetAt(dwID,CString(bstrIn));
	return S_OK;
}
