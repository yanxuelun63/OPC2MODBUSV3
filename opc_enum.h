#ifndef OPC_ENUM_H
#define OPC_ENUM_H

typedef struct HWND__* HWND;

#ifdef __cplusplus
extern "C" {
#endif

	int EnumRemoteOPCServers(HWND hList, const char* ip, const char* user, const char* pass);

#ifdef __cplusplus
}
#endif

#endif