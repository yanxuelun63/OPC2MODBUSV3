#ifndef OPC_BROWSE_H
#define OPC_BROWSE_H

#include "servers.h"

#ifdef __cplusplus
extern "C" {
#endif

	int BrowseAndExportItems(OPCServerInfo* srv, HWND hProgressWnd);

#ifdef __cplusplus
}
#endif

#endif