#ifndef SERVERS_H
#define SERVERS_H

#include <windows.h>
#include <ole2.h>

#define MAX_SERVERS 64

typedef DWORD OPCHANDLE;                    // 手动定义，替代 opcda.h

typedef struct IOPCServer IOPCServer;       // 前向声明
typedef struct IOPCGroupStateMgt IOPCGroupStateMgt;
typedef struct IOPCSyncIO IOPCSyncIO;

typedef struct {
    char name[64];
    char ip[64];
    wchar_t ip_wide[64];
    char progID[128];
    char clsid_str[128];   // 新增：CLSID 字符串（从枚举列表获取）
    char user[64];
    char pass_encrypted[1024];
    IOPCServer* pOPCServer;
    IOPCGroupStateMgt* pGroup;
    IOPCSyncIO* pSyncIO;
    OPCHANDLE hGroup;
    int connected;
    int remote;
    DWORD lastReadTime;
    int itemCount;
    char lastError[256];
    char lastTimeStr[32];
    ULONGLONG lastReconnectAttempt;
    DWORD reconnectDelay;
    int lastLogResult;       // 0=上次连接成功，1=上次连接失败，-1=未记录过
    int isTemporary; // 新增：1 表示临时连接（导出线程使用），0 表示主程序连接
} OPCServerInfo;

extern OPCServerInfo g_servers[MAX_SERVERS];
extern int g_serverCount;

int LoadServers(const char* path);

#endif