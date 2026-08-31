#ifndef MAPPING_H
#define MAPPING_H

#include <windows.h>
#include <stdint.h>
#include "servers.h"          // 引入 OPCHANDLE 定义

typedef enum {
    TYPE_BOOL,
    TYPE_WORD,
    TYPE_INT,       // 新增：16位有符号整型
    TYPE_DWORD,
    TYPE_LONG,      // 新增：32位有符号整型
    TYPE_FLOAT,
    TYPE_DOUBLE
} DataType;

typedef struct {
    int serverIndex;
    char itemID[256];
    DataType type;
    int modbusAddr;
    char byteOrder[9];       // 改为9字节，容纳8字符+结束符
    OPCHANDLE serverHandle;
    int modbusZone;
    char opcValue[32];
    int error;
    int lastVt;
    uint64_t rawValue;        // 新增：原始二进制值
} MappingItem;
extern MappingItem* g_mappings;
extern int g_mappingCount;
extern int g_mappingsCapacity;

extern uint8_t* g_coils;
extern int       g_coilsSize;
extern uint16_t* g_holdingRegs;
extern int       g_holdingRegsSize;

int LoadMapping(const char* csvPath);

#endif