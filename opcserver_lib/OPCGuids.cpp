// Define the COM GUIDs referenced by the OPC headers in exactly one TU.
// This provides the actual storage for extern GUID declarations such as
// IID_IOPCGroupStateMgt.
//
// Do NOT put INITGUID in a header. Keep it in one .cpp file only.
#include <rpc.h>       // defines EXTERN_C used by MIDL headers
#include <windows.h>
#include <initguid.h>

// OPC DA and related headers declare extern IIDs/CLSIDs.
// By including them after <initguid.h>, we emit their definitions.
#include "lib/opcda.h"      // IIDs like IID_IOPCServer, IID_IOPCGroupStateMgt, IID_IOPCItemMgt, IID_IOPCDataCallback, IID_IOPCSyncIO, IID_IOPCAsyncIO2, IID_IOPCBrowseServerAddressSpace
#include "lib/opccomn.h"    // Common OPC interfaces (if used)
#include "lib/opcEnum.h"    // CLSID_OpcServerList, IID_IOPCServerList (OPCEnum)