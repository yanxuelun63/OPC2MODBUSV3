#define WIN32_LEAN_AND_MEAN
#define _WIN32_DCOM
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <powrprof.h>   // 需要包含电源管理 API
#include <objbase.h>
#include <ole2.h>
#include <stdio.h>
#include <process.h>
#include <commctrl.h>
#include "opc_ids.h"
#include "config.h"
#include "servers.h"
#include "mapping.h"
#include "opc_client.h"
#include "modbus_server.h"
#include "ui.h"
#include "logger.h"
#include "mapping2.h"
#pragma comment(lib, "powrprof.lib")

HANDLE g_hMutex = NULL;   // 全局互斥体句柄
extern HWND g_hMainWnd;
extern INT_PTR CALLBACK ConfigDlgProc(HWND, UINT, WPARAM, LPARAM);
extern INT_PTR CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
volatile LONG g_running = 1;
CRITICAL_SECTION g_cs;
HANDLE g_hOPCThread = NULL;
HANDLE g_hModbusListenThread = NULL;
extern SOCKET g_listen_socket;

unsigned __stdcall OPCInitThread(void* param) {
    for (int i = 0; i < g_serverCount; i++) {
        if (OPC_Connect(&g_servers[i]) == 0)
            OPC_SetupGroupAndItems(&g_servers[i]);
        // 通知主窗口刷新状态
        if (g_hMainWnd)
            PostMessage(g_hMainWnd, WM_UPDATE_TABLE, 0, 0);
    }
    return 0;
}

unsigned __stdcall OPCThread(void *param) {
    while (g_running) {
        OPC_ReadAndUpdate();
        if (g_hMainWnd) PostMessage(g_hMainWnd, WM_UPDATE_TABLE, 0, 0);
        Sleep(g_cfg.refresh_ms);
    }
    return 0;
}

void StopGateway(void) {
    g_running = 0;

    // 立即关闭监听 socket，使 accept() 返回错误，从而退出监听线程
    if (g_listen_socket != INVALID_SOCKET) {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
    }
    // 等待导出线程结束
    if (g_hExportThread) {
        DWORD wait = WaitForSingleObject(g_hExportThread, 5000); // 最多等5秒
        if (wait == WAIT_TIMEOUT) {
            Logger_Write("导出线程未在退出前完成，将由操作系统清理。");
        }
        CloseHandle(g_hExportThread);
        g_hExportThread = NULL;
    }
    // 等待 Modbus 监听线程结束
    if (g_hModbusListenThread) {
        WaitForSingleObject(g_hModbusListenThread, INFINITE);
        CloseHandle(g_hModbusListenThread);
        g_hModbusListenThread = NULL;
    }
    // 等待 OPC 读取线程结束
    if (g_hOPCThread) {
        WaitForSingleObject(g_hOPCThread, INFINITE);
        CloseHandle(g_hOPCThread);
        g_hOPCThread = NULL;
    }

    // 断开所有 OPC 连接
    for (int i = 0; i < g_serverCount; i++) {
        OPC_Disconnect(&g_servers[i]);
    }
    g_serverCount = 0;

    // 释放mapping内存
    free(g_coils); g_coils = NULL;
    free(g_holdingRegs); g_holdingRegs = NULL;
    free(g_mappings); g_mappings = NULL;
    g_mappingCount = 0;
    g_mappingsCapacity = 0;
    // 释放 mapping2 资源
    free(g_coils2);          g_coils2 = NULL;
    free(g_holdingRegs2);    g_holdingRegs2 = NULL;
    free(g_mappings2);       g_mappings2 = NULL;
    g_mapping2Count = 0;
    g_mapping2Enabled = 0;
}
int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hPrev, _In_ LPSTR lpCmd, _In_ int nShow) {
    // 初始化日志系统（日志存放于 EXE 目录下的 logs 文件夹）
    Logger_Init(".\\logs");
    Logger_Write("程序启动");

    // 互斥体检测，防止多开
    g_hMutex = CreateMutex(NULL, FALSE, "OPC2ModbusV3_SingleInstance");
    if (g_hMutex == NULL) {
        Logger_Write("创建互斥体失败，可能允许多实例运行。");
    }
    else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, "程序已在运行中，请检查系统托盘。", "提示", MB_ICONINFORMATION);
        CloseHandle(g_hMutex);
        g_hMutex = NULL;
        return 0;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { MessageBox(NULL, "COM 初始化失败", "错误", MB_ICONERROR); return 1; }
    HRESULT hrSec = CoInitializeSecurity(NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_CONNECT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    if (FAILED(hrSec)) {
        Logger_Write("CoInitializeSecurity 失败: 0x%08X", hrSec);
    }

    // 初始化 Winsock（必须在任何 socket 操作之前）
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBox(NULL, "Winsock 初始化失败", "错误", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    // ★★★ 设置工作目录的代码 ★★★
    {
        char exeDir[MAX_PATH];
        GetModuleFileName(NULL, exeDir, MAX_PATH);
        char* p = strrchr(exeDir, '\\');
        if (p) {
            *p = '\0';                  // 去掉文件名，保留目录
            SetCurrentDirectory(exeDir);
        }
    }
    InitCommonControls();
    InitializeCriticalSection(&g_cs);

    // ========== 自动跳过配置界面的检查 ==========
    BOOL skipConfig = FALSE;
    LoadConfig(&g_cfg);   // 先加载配置，获得端口等参数
    // ========== 设置开机自启动（带路径校验和失败处理） ==========
    {
        char exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);
        char quotedPath[MAX_PATH + 3];
        sprintf(quotedPath, "\"%s\"", exePath);

        HKEY hKey;
        LONG regStatus = RegOpenKeyEx(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_READ | KEY_SET_VALUE, &hKey);

        if (regStatus == ERROR_SUCCESS) {
            char regPath[MAX_PATH + 3] = { 0 };
            DWORD regPathLen = sizeof(regPath);
            DWORD regType;
            LONG queryResult = RegQueryValueEx(hKey, "OPC2ModbusGateway", NULL,
                &regType, (BYTE*)regPath, &regPathLen);

            if (g_cfg.autoStart) {
                // 需要自启动
                if (queryResult == ERROR_SUCCESS && regType == REG_SZ &&
                    strcmp(regPath, quotedPath) == 0) {
                    // 路径一致，无需更改
                    Logger_Write("开机自启动路径已正确: %s", quotedPath);
                }
                else {
                    // 注册表项不存在或路径不一致，尝试更新
                    LONG setResult = RegSetValueEx(hKey, "OPC2ModbusGateway", 0, REG_SZ,
                        (BYTE*)quotedPath, (DWORD)(strlen(quotedPath) + 1));
                    if (setResult == ERROR_SUCCESS) {
                        Logger_Write("已更新开机自启动路径: %s", quotedPath);
                    }
                    else {
                        // 更新失败，删除可能存在的旧键值，并重置配置
                        RegDeleteValue(hKey, "OPC2ModbusGateway");
                        g_cfg.autoStart = 0;
                        SaveConfig(&g_cfg);
                        MessageBox(NULL, "开机自启动设置失败（可能因安全软件限制）。\n"
                            "已关闭自启动选项，程序将继续运行。",
                            "自启动失败", MB_ICONWARNING | MB_OK);
                        Logger_Write("开机自启动注册失败 (错误码=%ld)，已禁用 AutoStart", setResult);
                    }
                }
            }
            else {
                // config 要求关闭自启动，确保注册表项不存在
                if (queryResult == ERROR_SUCCESS) {
                    RegDeleteValue(hKey, "OPC2ModbusGateway");
                    Logger_Write("已删除开机自启动注册表项");
                }
            }
            RegCloseKey(hKey);
        }
        else {
            // 无法打开 Run 键
            if (g_cfg.autoStart) {
                g_cfg.autoStart = 0;
                SaveConfig(&g_cfg);
                MessageBox(NULL, "无法访问系统启动项注册表路径！\n"
                    "已自动关闭自启动选项，程序将继续运行。",
                    "自启动设置失败", MB_ICONWARNING | MB_OK);
                Logger_Write("无法打开注册表 Run 键 (错误码=%ld)，已禁用 AutoStart", regStatus);
            }
        }
    }
    // 检查三个必要文件是否存在
    if (GetFileAttributes("config.ini") != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributes("servers.csv") != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributes(g_cfg.mapping_path) != INVALID_FILE_ATTRIBUTES) {
        skipConfig = TRUE;
    }

    // 如果文件齐全，额外检查端口是否可用
    if (skipConfig) {
        SOCKET test_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (test_sock != INVALID_SOCKET) {
            struct sockaddr_in test_addr;
            memset(&test_addr, 0, sizeof(test_addr));
            test_addr.sin_family = AF_INET;
            test_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            test_addr.sin_port = htons(g_cfg.tcp_port);

            if (bind(test_sock, (struct sockaddr*)&test_addr, sizeof(test_addr)) == SOCKET_ERROR) {
                closesocket(test_sock);
                MessageBox(NULL, "配置的 Modbus TCP 端口已被占用，请重新设置。", "端口冲突", MB_ICONERROR | MB_OK);
                skipConfig = FALSE;  // 强制显示设置界面
            }
            else {
                closesocket(test_sock);
                Logger_Write("端口 %d 可用，跳过设置界面直接启动。", g_cfg.tcp_port);
            }
        }
        else {
            // socket 创建失败（极少见），也强制进入设置界面
            skipConfig = FALSE;
        }
    }

    // 根据需要弹出配置对话框
    if (!skipConfig) {
        if (DialogBox(hInst, MAKEINTRESOURCE(IDD_CONFIG_DIALOG), NULL, ConfigDlgProc) != IDOK) {
            // 用户取消或关闭配置对话框，退出程序
            StopGateway();
            DeleteCriticalSection(&g_cs);
            CoUninitialize();
            WSACleanup();
            if (g_hMutex != NULL) {
                CloseHandle(g_hMutex);
                g_hMutex = NULL;
            }
            Logger_Write("用户取消配置，程序退出。");
            Logger_Close();
            return 0;
        }
    }

    // ========== 公共启动流程（配置对话框或自动跳过都会执行） ==========
    LoadServers("servers.csv");
    LoadMapping(g_cfg.mapping_path);
    // 加载 mapping2（特殊应用场景）
    if (LoadMapping2("mapping2.csv") == 0) {
        g_mapping2Enabled = 1;
    }
    else {
        g_mapping2Enabled = 0;
    }

    // 创建并显示主窗口
    HWND hMain = CreateDialog(hInst, MAKEINTRESOURCE(IDD_MAIN_WINDOW), NULL, MainWndProc);
    if (!hMain) {
        StopGateway();
        DeleteCriticalSection(&g_cs);
        CoUninitialize();
        WSACleanup();
        if (g_hMutex != NULL) {
            CloseHandle(g_hMutex);
            g_hMutex = NULL;
        }
        Logger_Write("主窗口创建失败，程序退出。");
        Logger_Close();
        return 1;
    }
    ShowWindow(hMain, SW_SHOW);

    // 启动 Modbus TCP 监听线程
    g_hModbusListenThread = (HANDLE)_beginthreadex(NULL, 0, ModbusListenThread, NULL, 0, NULL);
    // 启动 OPC 读取线程（内部自动重连所有服务器）
    g_hOPCThread = (HANDLE)_beginthreadex(NULL, 0, OPCThread, NULL, 0, NULL);

    // 阻止系统待机
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理
    StopGateway();
    SetThreadExecutionState(ES_CONTINUOUS);
    DeleteCriticalSection(&g_cs);
    CoUninitialize();
    WSACleanup();

    if (g_hMutex != NULL) {
        CloseHandle(g_hMutex);
        g_hMutex = NULL;
    }
    Logger_Write("程序退出");
    Logger_Close();
    return 0;
}