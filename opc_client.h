#ifndef OPC_CLIENT_H
#define OPC_CLIENT_H

#include "servers.h"
#include "mapping.h"

int OPC_Connect(OPCServerInfo* srv);
int OPC_SetupGroupAndItems(OPCServerInfo* srv);
int OPC_ReadAndUpdate(void);
void OPC_Disconnect(OPCServerInfo* srv);

#endif