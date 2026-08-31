

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.01.0628 */
/* at Mon Jan 18 19:14:07 2038
 */
/* Compiler settings for D:/Work/OPC/OPC-Classic-CoreComponents/Source/Common/ServerEnumerator/OpcEnum.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.01.0628 
    protocol : dce , ms_ext, c_ext
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 440
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __OpcEnum_h__
#define __OpcEnum_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef DECLSPEC_XFGVIRT
#if defined(_CONTROL_FLOW_GUARD_XFG)
#define DECLSPEC_XFGVIRT(base, func) __declspec(xfg_virtual(base, func))
#else
#define DECLSPEC_XFGVIRT(base, func)
#endif
#endif

/* Forward Declarations */ 

#ifndef __OpcServerList_FWD_DEFINED__
#define __OpcServerList_FWD_DEFINED__

#ifdef __cplusplus
typedef class OpcServerList OpcServerList;
#else
typedef struct OpcServerList OpcServerList;
#endif /* __cplusplus */

#endif 	/* __OpcServerList_FWD_DEFINED__ */


/* header files for imported files */
#include "opccomn.h"

#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __OpcEnumLib_LIBRARY_DEFINED__
#define __OpcEnumLib_LIBRARY_DEFINED__

/* library OpcEnumLib */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_OpcEnumLib;

EXTERN_C const CLSID CLSID_OpcServerList;

#ifdef __cplusplus

class DECLSPEC_UUID("13486D51-4821-11D2-A494-3CB306C10000")
OpcServerList;
#endif
#endif /* __OpcEnumLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


