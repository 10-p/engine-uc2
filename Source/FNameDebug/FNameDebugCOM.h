/* this ALWAYS GENERATED file contains the definitions for the interfaces */


/* File created by MIDL compiler version 5.01.0164 */
/* at Sun Jun 09 09:58:48 2002
 */
/* Compiler settings for C:\UnrealWarfare\Build927\FNameDebugCOM\FNameDebugCOM.idl:
    Oicf (OptLev=i2), W1, Zp8, env=Win32, ms_ext, c_ext
    error checks: allocation ref bounds_check enum stub_data 
*/
//@@MIDL_FILE_HEADING(  )


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 440
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __FNameDebugCOM_h__
#define __FNameDebugCOM_h__

#ifdef __cplusplus
extern "C"{
#endif 

/* Forward Declarations */ 

#ifndef __IFNameDebug_FWD_DEFINED__
#define __IFNameDebug_FWD_DEFINED__
typedef interface IFNameDebug IFNameDebug;
#endif 	/* __IFNameDebug_FWD_DEFINED__ */


#ifndef __FNameDebug_FWD_DEFINED__
#define __FNameDebug_FWD_DEFINED__

#ifdef __cplusplus
typedef class FNameDebug FNameDebug;
#else
typedef struct FNameDebug FNameDebug;
#endif /* __cplusplus */

#endif 	/* __FNameDebug_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

void __RPC_FAR * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void __RPC_FAR * ); 

#ifndef __IFNameDebug_INTERFACE_DEFINED__
#define __IFNameDebug_INTERFACE_DEFINED__

/* interface IFNameDebug */
/* [unique][helpstring][dual][uuid][object] */ 


EXTERN_C const IID IID_IFNameDebug;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("2FFAD760-4E7C-4472-BA61-462B8EBE0FCF")
    IFNameDebug : public IDispatch
    {
    public:
        virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_FName( 
            DWORD dwID,
            /* [retval][out] */ BSTR __RPC_FAR *pVal) = 0;
        
        virtual /* [helpstring][id][propput] */ HRESULT STDMETHODCALLTYPE put_FName( 
            DWORD dwID,
            /* [in] */ BSTR newVal) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IFNameDebugVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )( 
            IFNameDebug __RPC_FAR * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject);
        
        ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )( 
            IFNameDebug __RPC_FAR * This);
        
        ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )( 
            IFNameDebug __RPC_FAR * This);
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetTypeInfoCount )( 
            IFNameDebug __RPC_FAR * This,
            /* [out] */ UINT __RPC_FAR *pctinfo);
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetTypeInfo )( 
            IFNameDebug __RPC_FAR * This,
            /* [in] */ UINT iTInfo,
            /* [in] */ LCID lcid,
            /* [out] */ ITypeInfo __RPC_FAR *__RPC_FAR *ppTInfo);
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *GetIDsOfNames )( 
            IFNameDebug __RPC_FAR * This,
            /* [in] */ REFIID riid,
            /* [size_is][in] */ LPOLESTR __RPC_FAR *rgszNames,
            /* [in] */ UINT cNames,
            /* [in] */ LCID lcid,
            /* [size_is][out] */ DISPID __RPC_FAR *rgDispId);
        
        /* [local] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *Invoke )( 
            IFNameDebug __RPC_FAR * This,
            /* [in] */ DISPID dispIdMember,
            /* [in] */ REFIID riid,
            /* [in] */ LCID lcid,
            /* [in] */ WORD wFlags,
            /* [out][in] */ DISPPARAMS __RPC_FAR *pDispParams,
            /* [out] */ VARIANT __RPC_FAR *pVarResult,
            /* [out] */ EXCEPINFO __RPC_FAR *pExcepInfo,
            /* [out] */ UINT __RPC_FAR *puArgErr);
        
        /* [helpstring][id][propget] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *get_FName )( 
            IFNameDebug __RPC_FAR * This,
            DWORD dwID,
            /* [retval][out] */ BSTR __RPC_FAR *pVal);
        
        /* [helpstring][id][propput] */ HRESULT ( STDMETHODCALLTYPE __RPC_FAR *put_FName )( 
            IFNameDebug __RPC_FAR * This,
            DWORD dwID,
            /* [in] */ BSTR newVal);
        
        END_INTERFACE
    } IFNameDebugVtbl;

    interface IFNameDebug
    {
        CONST_VTBL struct IFNameDebugVtbl __RPC_FAR *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IFNameDebug_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IFNameDebug_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IFNameDebug_Release(This)	\
    (This)->lpVtbl -> Release(This)


#define IFNameDebug_GetTypeInfoCount(This,pctinfo)	\
    (This)->lpVtbl -> GetTypeInfoCount(This,pctinfo)

#define IFNameDebug_GetTypeInfo(This,iTInfo,lcid,ppTInfo)	\
    (This)->lpVtbl -> GetTypeInfo(This,iTInfo,lcid,ppTInfo)

#define IFNameDebug_GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)	\
    (This)->lpVtbl -> GetIDsOfNames(This,riid,rgszNames,cNames,lcid,rgDispId)

#define IFNameDebug_Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)	\
    (This)->lpVtbl -> Invoke(This,dispIdMember,riid,lcid,wFlags,pDispParams,pVarResult,pExcepInfo,puArgErr)


#define IFNameDebug_get_FName(This,dwID,pVal)	\
    (This)->lpVtbl -> get_FName(This,dwID,pVal)

#define IFNameDebug_put_FName(This,dwID,newVal)	\
    (This)->lpVtbl -> put_FName(This,dwID,newVal)

#endif /* COBJMACROS */


#endif 	/* C style interface */



/* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE IFNameDebug_get_FName_Proxy( 
    IFNameDebug __RPC_FAR * This,
    DWORD dwID,
    /* [retval][out] */ BSTR __RPC_FAR *pVal);


void __RPC_STUB IFNameDebug_get_FName_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);


/* [helpstring][id][propput] */ HRESULT STDMETHODCALLTYPE IFNameDebug_put_FName_Proxy( 
    IFNameDebug __RPC_FAR * This,
    DWORD dwID,
    /* [in] */ BSTR newVal);


void __RPC_STUB IFNameDebug_put_FName_Stub(
    IRpcStubBuffer *This,
    IRpcChannelBuffer *_pRpcChannelBuffer,
    PRPC_MESSAGE _pRpcMessage,
    DWORD *_pdwStubPhase);



#endif 	/* __IFNameDebug_INTERFACE_DEFINED__ */



#ifndef __FNAMEDEBUGCOMLib_LIBRARY_DEFINED__
#define __FNAMEDEBUGCOMLib_LIBRARY_DEFINED__

/* library FNAMEDEBUGCOMLib */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_FNAMEDEBUGCOMLib;

EXTERN_C const CLSID CLSID_FNameDebug;

#ifdef __cplusplus

class DECLSPEC_UUID("53E96B9B-3784-4EDC-A80D-268CB5B5EC0D")
FNameDebug;
#endif
#endif /* __FNAMEDEBUGCOMLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

unsigned long             __RPC_USER  BSTR_UserSize(     unsigned long __RPC_FAR *, unsigned long            , BSTR __RPC_FAR * ); 
unsigned char __RPC_FAR * __RPC_USER  BSTR_UserMarshal(  unsigned long __RPC_FAR *, unsigned char __RPC_FAR *, BSTR __RPC_FAR * ); 
unsigned char __RPC_FAR * __RPC_USER  BSTR_UserUnmarshal(unsigned long __RPC_FAR *, unsigned char __RPC_FAR *, BSTR __RPC_FAR * ); 
void                      __RPC_USER  BSTR_UserFree(     unsigned long __RPC_FAR *, BSTR __RPC_FAR * ); 

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif
