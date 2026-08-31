#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "logger.h"

static FILE *g_logFile = NULL;
static char g_logDir[MAX_PATH] = "";
static int g_currentMonth = -1;

// 清理超过半年的日志文件
static void CleanOldLogs(void) {
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    char searchPath[MAX_PATH];
    sprintf(searchPath, "%s\\*.log", g_logDir);
    hFind = FindFirstFile(searchPath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    SYSTEMTIME st;
    GetSystemTime(&st);
    FILETIME ftNow;
    SystemTimeToFileTime(&st, &ftNow);
    ULARGE_INTEGER uliNow;
    uliNow.LowPart = ftNow.dwLowDateTime;
    uliNow.HighPart = ftNow.dwHighDateTime;
    
    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        FILETIME ftCreate = findData.ftCreationTime;
        ULARGE_INTEGER uliCreate;
        uliCreate.LowPart = ftCreate.dwLowDateTime;
        uliCreate.HighPart = ftCreate.dwHighDateTime;
        
        // 半年约 183 天，100纳秒单位
        ULONGLONG diff = uliNow.QuadPart - uliCreate.QuadPart;
        if (diff > 183ULL * 24 * 3600 * 10000000) {
            char fullPath[MAX_PATH];
            sprintf(fullPath, "%s\\%s", g_logDir, findData.cFileName);
            DeleteFile(fullPath);
        }
    } while (FindNextFile(hFind, &findData));
    FindClose(hFind);
}

int Logger_Init(const char *logDir) {
    strncpy(g_logDir, logDir, MAX_PATH - 1);
    // 创建日志目录（如果不存在）
    CreateDirectory(logDir, NULL);
    // 清理过期日志
    CleanOldLogs();
    return 0;
}

static void OpenLogFileIfNeeded(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    int month = st.wMonth + st.wYear * 100;
    if (g_logFile && month == g_currentMonth) return;  // 同一月，无需重新打开
    
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = NULL;
    }
    
    char filename[MAX_PATH];
    sprintf(filename, "%s\\OPC2Modbus_%04d%02d.log", g_logDir, st.wYear, st.wMonth);
    g_logFile = fopen(filename, "a");  // 追加模式
    g_currentMonth = month;
}

void Logger_Write(const char *format, ...) {
    OpenLogFileIfNeeded();
    if (!g_logFile) return;
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_logFile, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond);
    
    va_list args;
    va_start(args, format);
    vfprintf(g_logFile, format, args);
    va_end(args);
    
    fprintf(g_logFile, "\n");
    fflush(g_logFile);  // 立即写入磁盘，防止丢失
}

void Logger_Close(void) {
    if (g_logFile) {
        fclose(g_logFile);
        g_logFile = NULL;
    }
}