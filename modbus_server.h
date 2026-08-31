#ifndef MODBUS_SERVER_H
#define MODBUS_SERVER_H

#include <winsock2.h>

extern SOCKET g_listen_socket;
unsigned __stdcall ModbusListenThread(void* param);

#endif