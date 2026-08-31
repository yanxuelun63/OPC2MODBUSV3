#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include "servers.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   // 为 malloc/free 添加

OPCServerInfo g_servers[MAX_SERVERS];
int g_serverCount = 0;

// 从一行 CSV 文本中提取第 n 列（0-based）
static void get_csv_column(const char* line, int index, char* dest, int dest_size) {
    const char* start = line;
    for (int i = 0; i < index; i++) {
        start = strchr(start, ',');
        if (!start) { dest[0] = '\0'; return; }
        start++; // 跳过逗号
    }
    const char* end = strchr(start, ',');
    if (!end) end = line + strlen(line);   // 最后一列
    int len = (int)(end - start);
    if (len >= dest_size) len = dest_size - 1;
    memcpy(dest, start, len);
    dest[len] = '\0';
}

int LoadServers(const char* path) {
    // 备份当前已连接服务器的关键运行时状态（堆分配，避免栈溢出）
    typedef struct {
        char name[64];
        IOPCServer* pOPCServer;
        IOPCSyncIO* pSyncIO;
        OPCHANDLE hGroup;
        int connected;
        int itemCount;
        char lastError[256];
        char lastTimeStr[32];
        ULONGLONG lastReconnectAttempt;
        DWORD reconnectDelay;
        int lastLogResult;
    } ServerBackup;

    ServerBackup* backups = (ServerBackup*)malloc(MAX_SERVERS * sizeof(ServerBackup));
    if (backups) {
        memset(backups, 0, MAX_SERVERS * sizeof(ServerBackup));  // 消除 C6001
    }
    int backupCount = 0;

    // 备份当前已连接的服务器的运行时状态
    if (backups) {
        for (int i = 0; i < g_serverCount; i++) {
            if (g_servers[i].connected) {
                strncpy(backups[backupCount].name, g_servers[i].name, sizeof(backups[0].name) - 1);
                backups[backupCount].pOPCServer = g_servers[i].pOPCServer;
                backups[backupCount].pSyncIO = g_servers[i].pSyncIO;
                backups[backupCount].hGroup = g_servers[i].hGroup;
                backups[backupCount].connected = g_servers[i].connected;
                backups[backupCount].itemCount = g_servers[i].itemCount;
                strncpy(backups[backupCount].lastError, g_servers[i].lastError, sizeof(backups[0].lastError) - 1);
                strncpy(backups[backupCount].lastTimeStr, g_servers[i].lastTimeStr, sizeof(backups[0].lastTimeStr) - 1);
                backups[backupCount].lastReconnectAttempt = g_servers[i].lastReconnectAttempt;
                backups[backupCount].reconnectDelay = g_servers[i].reconnectDelay;
                backups[backupCount].lastLogResult = g_servers[i].lastLogResult;
                backupCount++;
            }
        }
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        free(backups);   // 打开失败，释放堆内存
        return -1;
    }
    char line[2048];
    // 跳过表头
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        free(backups);   // 读取失败，释放堆内存
        return 0;
    }
    g_serverCount = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0' || line[0] == '#') continue;

        char name[64] = "", ip[64] = "", prog[128] = "", user[64] = "", pass[1024] = "", clsid[128] = "";
        // 手动分割每一列
        get_csv_column(line, 0, name, sizeof(name));
        get_csv_column(line, 1, ip, sizeof(ip));
        get_csv_column(line, 2, prog, sizeof(prog));
        get_csv_column(line, 3, user, sizeof(user));
        get_csv_column(line, 4, pass, sizeof(pass));
        get_csv_column(line, 5, clsid, sizeof(clsid));

        // 至少需要名称、IP 和 ProgID/CLSID
        if (name[0] == '\0' || ip[0] == '\0' || (prog[0] == '\0' && clsid[0] == '\0'))
            continue;

        if (g_serverCount >= MAX_SERVERS) break;
        memset(&g_servers[g_serverCount], 0, sizeof(OPCServerInfo));

        strncpy(g_servers[g_serverCount].name, name, sizeof(g_servers[g_serverCount].name) - 1);
        g_servers[g_serverCount].name[sizeof(g_servers[g_serverCount].name) - 1] = '\0';

        strncpy(g_servers[g_serverCount].ip, ip, sizeof(g_servers[g_serverCount].ip) - 1);
        g_servers[g_serverCount].ip[sizeof(g_servers[g_serverCount].ip) - 1] = '\0';

        MultiByteToWideChar(CP_ACP, 0, ip, -1, g_servers[g_serverCount].ip_wide, 64);

        strncpy(g_servers[g_serverCount].progID, prog, sizeof(g_servers[g_serverCount].progID) - 1);
        g_servers[g_serverCount].progID[sizeof(g_servers[g_serverCount].progID) - 1] = '\0';

        strncpy(g_servers[g_serverCount].clsid_str, clsid, sizeof(g_servers[g_serverCount].clsid_str) - 1);
        g_servers[g_serverCount].clsid_str[sizeof(g_servers[g_serverCount].clsid_str) - 1] = '\0';

        strncpy(g_servers[g_serverCount].user, user, sizeof(g_servers[g_serverCount].user) - 1);
        g_servers[g_serverCount].user[sizeof(g_servers[g_serverCount].user) - 1] = '\0';

        strncpy(g_servers[g_serverCount].pass_encrypted, pass, sizeof(g_servers[g_serverCount].pass_encrypted) - 1);
        g_servers[g_serverCount].pass_encrypted[sizeof(g_servers[g_serverCount].pass_encrypted) - 1] = '\0';

        g_servers[g_serverCount].remote = (strcmp(ip, "127.0.0.1") != 0 && strcmp(ip, "localhost") != 0 && ip[0] != '\0');
        g_servers[g_serverCount].lastLogResult = -1;
        g_servers[g_serverCount].reconnectDelay = 10000;   // 初始10秒
        g_serverCount++;
    }
    fclose(fp);

    // 恢复已连接服务器的运行时状态
    if (backups) {
        for (int i = 0; i < g_serverCount; i++) {
            for (int j = 0; j < backupCount; j++) {
                if (strcmp(g_servers[i].name, backups[j].name) == 0) {
                    g_servers[i].pOPCServer = backups[j].pOPCServer;
                    g_servers[i].pSyncIO = backups[j].pSyncIO;
                    g_servers[i].hGroup = backups[j].hGroup;
                    g_servers[i].connected = backups[j].connected;
                    g_servers[i].itemCount = backups[j].itemCount;
                    strncpy(g_servers[i].lastError, backups[j].lastError, sizeof(g_servers[i].lastError) - 1);
                    strncpy(g_servers[i].lastTimeStr, backups[j].lastTimeStr, sizeof(g_servers[i].lastTimeStr) - 1);
                    g_servers[i].lastReconnectAttempt = backups[j].lastReconnectAttempt;
                    g_servers[i].reconnectDelay = backups[j].reconnectDelay;
                    g_servers[i].lastLogResult = backups[j].lastLogResult;
                    break;
                }
            }
        }
        free(backups);  // 释放堆内存
    }

    return 0;
}