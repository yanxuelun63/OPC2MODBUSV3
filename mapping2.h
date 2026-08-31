#ifndef MAPPING2_H
#define MAPPING2_H
#include <stdint.h>      // 新增：提供 uint8_t、uint16_t 定义
#include <windows.h>
#include "servers.h"

// 最大条目数（动态扩展）
#define MAPPING2_INIT_CAP 64

typedef struct {
    int serverIndex;
    char itemID[256];
    int modbusAddr;
    float factor;
    char byteOrder[9];        // 字节序，如 "ABCD"
    OPCHANDLE serverHandle;   // 新增：OPC 项句柄
    // 解析后的数据存储
    uint8_t qualityBit;       // bit15-12 正常标志，保存最近一次读取的质量位
    uint16_t rawValue;        // 原始 Word 值
    float computedFloat;      // 计算后的浮点值（bit11-0 解析 * factor）
    int coilsBase;            // 线圈基地址 (modbusAddr/2*5)
} Mapping2Item;

extern Mapping2Item *g_mappings2;
extern int g_mapping2Count;
extern int g_mapping2Enabled;

// 分配额外的线圈和保持寄存器数组
extern uint8_t  *g_coils2;
extern int       g_coils2Size;
extern uint16_t *g_holdingRegs2;
extern int       g_holdingRegs2Size;

int LoadMapping2(const char *csvPath);

#endif