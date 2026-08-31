#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapping2.h"
#include "servers.h"   // g_servers, g_serverCount

Mapping2Item *g_mappings2 = NULL;
int g_mapping2Count = 0;
int g_mapping2Enabled = 0;

uint8_t  *g_coils2 = NULL;
int       g_coils2Size = 0;
uint16_t *g_holdingRegs2 = NULL;
int       g_holdingRegs2Size = 0;

static int mapping2Capacity = 0;

int LoadMapping2(const char *csvPath) {
    // 释放旧资源
    free(g_mappings2); g_mappings2 = NULL; g_mapping2Count = 0; mapping2Capacity = 0;
    free(g_coils2); free(g_holdingRegs2); g_coils2 = NULL; g_holdingRegs2 = NULL;
    g_coils2Size = 0; g_holdingRegs2Size = 0;
    g_mapping2Enabled = 0;

    FILE *fp = fopen(csvPath, "r");
    if (!fp) return -1;

    char line[512];
    // 跳过表头
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }

    int maxCoil = -1, maxHolding = -1;
    int initCap = MAPPING2_INIT_CAP;
    g_mappings2 = (Mapping2Item*)malloc(initCap * sizeof(Mapping2Item));
    if (!g_mappings2) { fclose(fp); return -1; }
    mapping2Capacity = initCap;
    g_mapping2Count = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0' || line[0] == '#') continue;

        char srv[64], item[256], order[9] = "";
        int addr;
        float factor;
        // 解析：ServerName,OPCItemID,ModbusAddr,Factor,ByteOrder
        if (sscanf(line, "%63[^,],%255[^,],%d,%f,%8s", srv, item, &addr, &factor, order) < 4) continue;

        // 查找服务器索引
        int srvIdx = -1;
        for (int i = 0; i < g_serverCount; i++) {
            if (strcmp(g_servers[i].name, srv) == 0) {
                srvIdx = i;
                break;
            }
        }
        if (srvIdx == -1) continue;

        // 动态扩容
        if (g_mapping2Count >= mapping2Capacity) {
            mapping2Capacity *= 2;
            Mapping2Item *tmp = realloc(g_mappings2, mapping2Capacity * sizeof(Mapping2Item));
            if (!tmp) { free(g_mappings2); fclose(fp); return -1; }
            g_mappings2 = tmp;
        }

        Mapping2Item *p = &g_mappings2[g_mapping2Count];
        p->serverHandle = 0;   // 稍后由 OPC_SetupGroupAndItems 填充
        p->serverIndex = srvIdx;
        strncpy(p->itemID, item, sizeof(p->itemID)-1);
        p->itemID[sizeof(p->itemID)-1] = '\0';
        p->modbusAddr = addr;
        p->factor = factor;
        strncpy(p->byteOrder, order, sizeof(p->byteOrder)-1);
        p->byteOrder[sizeof(p->byteOrder)-1] = '\0';
        p->qualityBit = 0;
        p->rawValue = 0;
        p->computedFloat = 0.0f;

        // 计算线圈基地址和最大地址
        int coilsBase = addr / 2 * 5;
        p->coilsBase = coilsBase;
        int coilMax = coilsBase + 4;   // 使用 +0..+3 和 +5
        if (coilMax > maxCoil) maxCoil = coilMax;

        // 保持寄存器占用 2 个（浮点数）
        int holdingLast = addr + 1;
        if (holdingLast > maxHolding) maxHolding = holdingLast;

        g_mapping2Count++;
    }
    fclose(fp);

    // 分配线圈和保持寄存器数组
    if (maxCoil >= 0) {
        g_coils2Size = maxCoil + 1;
        g_coils2 = (uint8_t*)calloc(g_coils2Size, 1);
    }
    if (maxHolding >= 0) {
        g_holdingRegs2Size = maxHolding + 1;
        g_holdingRegs2 = (uint16_t*)calloc(g_holdingRegs2Size, sizeof(uint16_t));
    }

    g_mapping2Enabled = (g_mapping2Count > 0) ? 1 : 0;
    return g_mapping2Count > 0 ? 0 : -1;
}