// FNameDebug.h: Definition of the FNameDebug class
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_FNAMEDEBUG_H__2AC576ED_E5EA_4828_BFB4_0AFF403A30D4__INCLUDED_)
#define AFX_FNAMEDEBUG_H__2AC576ED_E5EA_4828_BFB4_0AFF403A30D4__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "resource.h"       // main symbols

// Maintains the map of IDs to CStrings
typedef CMap<DWORD,DWORD&,CString,CString&> CMapIdToString;

/////////////////////////////////////////////////////////////////////////////
// FNameDebug

class FNameDebug : 
	public IDispatchImpl<IFNameDebug, &IID_IFNameDebug, &LIBID_FNAMEDEBUGCOMLib>, 
	public ISupportErrorInfo,
	public CComObjectRoot,
	public CComCoClass<FNameDebug,&CLSID_FNameDebug>
{
	// Since the map is static, this needs to be thread safe
	CComAutoCriticalSection m_csAccessSync;
	// Maps the FName's ID to it's string
	static CMapIdToString m_mapIdToString;

public:
	FNameDebug() {}
BEGIN_COM_MAP(FNameDebug)
	COM_INTERFACE_ENTRY(IDispatch)
	COM_INTERFACE_ENTRY(IFNameDebug)
	COM_INTERFACE_ENTRY(ISupportErrorInfo)
END_COM_MAP()

DECLARE_NOT_AGGREGATABLE(FNameDebug) 

DECLARE_REGISTRY_RESOURCEID(IDR_FNameDebug);
// ISupportsErrorInfo
	STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid);

// IFNameDebug
public:
	STDMETHOD(get_FName)(DWORD dwID, /*[out, retval]*/ BSTR *pVal);
	STDMETHOD(put_FName)(DWORD dwID, /*[in]*/ BSTR newVal);
};

#endif // !defined(AFX_FNAMEDEBUG_H__2AC576ED_E5EA_4828_BFB4_0AFF403A30D4__INCLUDED_)
