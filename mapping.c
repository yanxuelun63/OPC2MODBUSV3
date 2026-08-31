#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include "mapping.h"
#include "servers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MappingItem* g_mappings = NULL;
int g_mappingCount = 0;
int g_mappingsCapacity = 0;
uint8_t* g_coils = NULL;
int       g_coilsSize = 0;
uint16_t* g_holdingRegs = NULL;
int       g_holdingRegsSize = 0;

int LoadMapping(const char* csvPath) {
    if (g_mappings) { free(g_mappings); g_mappings = NULL; g_mappingCount = g_mappingsCapacity = 0; }
    FILE* fp = fopen(csvPath, "r");
    if (!fp) return -1;
    char line[512];
    if (fgets(line, sizeof(line), fp) == NULL) { fclose(fp); return -1; }
    g_mappingCount = 0;
    int maxCoil = -1, maxHolding = -1;
    int initCap = 512;
    g_mappings = (MappingItem*)malloc(initCap * sizeof(MappingItem));
    if (!g_mappings) { fclose(fp); return -1; }
    g_mappingsCapacity = initCap;
    for (int i = 0; i < g_serverCount; i++) g_servers[i].itemCount = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0' || line[0] == '#') continue;
        char srv[64], item[256], typeStr[16], order[9] = "";
        int addr;
        if (sscanf(line, "%63[^,],%255[^,],%15[^,],%d,%8s", srv, item, typeStr, &addr, order) < 4) continue;
        if (g_mappingCount >= g_mappingsCapacity) {
            int newCap = g_mappingsCapacity * 2;
            MappingItem* tmp = realloc(g_mappings, newCap * sizeof(MappingItem));
            if (!tmp) { free(g_mappings); g_mappings = NULL; fclose(fp); return -1; }
            g_mappings = tmp; g_mappingsCapacity = newCap;
        }
        int srvIdx = -1;
        for (int i = 0; i < g_serverCount; i++)
            if (strcmp(g_servers[i].name, srv) == 0) { srvIdx = i; break; }
        if (srvIdx == -1) continue;
        DataType dt;
        if (strcmp(typeStr, "Bool") == 0) dt = TYPE_BOOL;
        else if (strcmp(typeStr, "Word") == 0) dt = TYPE_WORD;
        else if (strcmp(typeStr, "Int") == 0) dt = TYPE_INT;
        else if (strcmp(typeStr, "DWord") == 0) dt = TYPE_DWORD;
        else if (strcmp(typeStr, "Long") == 0) dt = TYPE_LONG;
        else if (strcmp(typeStr, "Float") == 0) dt = TYPE_FLOAT;
        else if (strcmp(typeStr, "Double") == 0) dt = TYPE_DOUBLE;
        else continue;
        MappingItem* p = &g_mappings[g_mappingCount];
        p->rawValue = 0;         // 新增
        p->serverIndex = srvIdx;
        strncpy(p->itemID, item, sizeof(p->itemID) - 1);
        p->type = dt; p->modbusAddr = addr;
        strncpy(p->byteOrder, order, sizeof(p->byteOrder) - 1);
        p->byteOrder[sizeof(p->byteOrder) - 1] = '\0';
        p->serverHandle = 0;
        p->modbusZone = (dt == TYPE_BOOL) ? 0 : 4;
        p->error = 0; p->opcValue[0] = '\0'; p->lastVt = 0;
        if (dt == TYPE_BOOL) { if (addr > maxCoil) maxCoil = addr; }
        else {
            int last = addr;
            if (dt == TYPE_DWORD || dt == TYPE_FLOAT || dt == TYPE_LONG) last = addr + 1;
            else if (dt == TYPE_DOUBLE) last = addr + 3;
            if (last > maxHolding) maxHolding = last;
        }
        g_servers[srvIdx].itemCount++;
        g_mappingCount++;
    }
    fclose(fp);

    // 安全分配线圈数组
    g_coilsSize = maxCoil + 1;
    if (g_coilsSize > 0) {
        uint8_t* tmp_coils = realloc(g_coils, g_coilsSize);
        if (tmp_coils) {
            g_coils = tmp_coils;
            memset(g_coils, 0, g_coilsSize);
        }
        // 如果 tmp_coils 为 NULL，保留原指针 g_coils，不造成泄漏，但无法更新到新大小
    }
    else {
        // size 为 0，释放原内存（如果有）并置空
        free(g_coils);
        g_coils = NULL;
    }

    // 安全分配保持寄存器数组
    g_holdingRegsSize = maxHolding + 1;
    if (g_holdingRegsSize > 0) {
        uint16_t* tmp_holding = realloc(g_holdingRegs, g_holdingRegsSize * sizeof(uint16_t));
        if (tmp_holding) {
            g_holdingRegs = tmp_holding;
            memset(g_holdingRegs, 0, g_holdingRegsSize * sizeof(uint16_t));
        }
        // 分配失败则保留原指针
    }
    else {
        free(g_holdingRegs);
        g_holdingRegs = NULL;
    }

    return 0;
}