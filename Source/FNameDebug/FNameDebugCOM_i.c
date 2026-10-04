/* this file contains the actual definitions of */
/* the IIDs and CLSIDs */

/* link this file in with the server and any clients */


/* File created by MIDL compiler version 5.01.0164 */
/* at Sun Jun 09 09:58:48 2002
 */
/* Compiler settings for C:\UnrealWarfare\Build927\FNameDebugCOM\FNameDebugCOM.idl:
    Oicf (OptLev=i2), W1, Zp8, env=Win32, ms_ext, c_ext
    error checks: allocation ref bounds_check enum stub_data 
*/
//@@MIDL_FILE_HEADING(  )
#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __IID_DEFINED__
#define __IID_DEFINED__

typedef struct _IID
{
    unsigned long x;
    unsigned short s1;
    unsigned short s2;
    unsigned char  c[8];
} IID;

#endif // __IID_DEFINED__

#ifndef CLSID_DEFINED
#define CLSID_DEFINED
typedef IID CLSID;
#endif // CLSID_DEFINED

const IID IID_IFNameDebug = {0x2FFAD760,0x4E7C,0x4472,{0xBA,0x61,0x46,0x2B,0x8E,0xBE,0x0F,0xCF}};


const IID LIBID_FNAMEDEBUGCOMLib = {0x47D11202,0xB410,0x49F6,{0xBE,0x2F,0x91,0xD8,0x5F,0x23,0x4C,0x45}};


const CLSID CLSID_FNameDebug = {0x53E96B9B,0x3784,0x4EDC,{0xA8,0x0D,0x26,0x8C,0xB5,0xB5,0xEC,0x0D}};


#ifdef __cplusplus
}
#endif

