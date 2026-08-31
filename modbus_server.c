#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <process.h>
#include "modbus_server.h"
#include "config.h"
#include "mapping.h"
#include "logger.h"
#include "mapping2.h"

extern volatile LONG g_running;
extern CRITICAL_SECTION g_cs;

SOCKET g_listen_socket = INVALID_SOCKET;

// 发送 Modbus 异常响应
void SendException(SOCKET client, uint16_t transID, uint8_t func, uint8_t code) {
    uint8_t buf[9];
    buf[0] = transID >> 8;
    buf[1] = transID & 0xFF;
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 0;
    buf[5] = 3;
    buf[6] = 0;            // 从站地址（异常响应中通常为 0）
    buf[7] = func | 0x80;
    buf[8] = code;
    send(client, (char*)buf, 9, 0);
}

void ModbusClientThread(void* param) {
    int client = (int)(intptr_t)param;
    // 获取客户端 IP
    struct sockaddr_in peer;
    int peerLen = sizeof(peer);
    getpeername(client, (struct sockaddr*)&peer, &peerLen);
    char* peerIP = inet_ntoa(peer.sin_addr);
    Logger_Write("Modbus客户端连接: IP=%s", peerIP);

    uint8_t req[256];
    while (g_running) {
        int len = recv(client, (char*)req, sizeof(req), 0);
        if (len <= 0) {
            Logger_Write("Modbus客户端断开: IP=%s", peerIP);
            break;
        }

        if (len < 8) continue;

        uint16_t transID = (req[0] << 8) | req[1];
        uint8_t  slaveID = req[6];
        uint8_t  func = req[7];

        // 选择数据表
        uint8_t* coils_ptr = NULL;
        uint16_t* holding_ptr = NULL;
        int       coils_size = 0;
        int       holding_size = 0;

        if (slaveID == 0 || slaveID == g_cfg.slave_id) {
            coils_ptr = g_coils;
            holding_ptr = g_holdingRegs;
            coils_size = g_coilsSize;
            holding_size = g_holdingRegsSize;
        }
        else if (g_mapping2Enabled && slaveID == g_cfg.slave_id + 1) {
            coils_ptr = g_coils2;
            holding_ptr = g_holdingRegs2;
            coils_size = g_coils2Size;
            holding_size = g_holdingRegs2Size;
        }
        else {
            // 从站ID不匹配，忽略请求（不发送异常，也不记录日志，避免刷屏）
            continue;
        }

        EnterCriticalSection(&g_cs);

        // 功能码 03/04：读保持/输入寄存器
        if (func == 0x03 || func == 0x04) {
            uint16_t startAddr = (req[8] << 8) | req[9];
            uint16_t quantity = (req[10] << 8) | req[11];
            if (quantity < 1 || quantity > 125 || startAddr + quantity > holding_size) {
                Logger_Write("Modbus请求异常: 从站ID=%d, 功能码=%02X, 起始地址=%d, 数量=%d, 超出范围 (最大地址=%d)",
                    slaveID, func, startAddr, quantity, holding_size - 1);
                LeaveCriticalSection(&g_cs);
                SendException(client, transID, func, 0x02);
                continue;
            }
            uint8_t response[256];
            response[0] = transID >> 8; response[1] = transID & 0xFF;
            response[2] = 0; response[3] = 0;
            response[4] = (uint8_t)((3 + quantity * 2) >> 8);
            response[5] = (uint8_t)(3 + quantity * 2);
            response[6] = slaveID;
            response[7] = func;
            response[8] = (uint8_t)(quantity * 2);
            for (uint16_t i = 0; i < quantity; i++) {
                uint16_t val = holding_ptr[startAddr + i];
                response[9 + i * 2] = (val >> 8) & 0xFF;
                response[9 + i * 2 + 1] = val & 0xFF;
            }
            LeaveCriticalSection(&g_cs);
            send(client, (char*)response, 9 + quantity * 2, 0);
        }
        // 功能码 01/02：读线圈/离散输入
        else if (func == 0x01 || func == 0x02) {
            uint16_t startAddr = (req[8] << 8) | req[9];
            uint16_t quantity = (req[10] << 8) | req[11];
            if (quantity < 1 || quantity > 2000 || startAddr + quantity > coils_size) {
                Logger_Write("Modbus请求异常: 从站ID=%d, 功能码=%02X, 起始地址=%d, 数量=%d, 超出范围 (最大地址=%d)",
                    slaveID, func, startAddr, quantity, coils_size - 1);
                LeaveCriticalSection(&g_cs);
                SendException(client, transID, func, 0x02);
                continue;
            }
            uint8_t byteCount = (quantity + 7) / 8;
            uint8_t response[256];
            response[0] = transID >> 8; response[1] = transID & 0xFF;
            response[2] = 0; response[3] = 0;
            response[4] = (uint8_t)((3 + byteCount) >> 8);
            response[5] = (uint8_t)(3 + byteCount);
            response[6] = slaveID;
            response[7] = func;
            response[8] = byteCount;
            uint8_t bitData[256] = { 0 };
            for (uint16_t i = 0; i < quantity; i++) {
                if (coils_ptr[startAddr + i])
                    bitData[i / 8] |= (1 << (i % 8));
            }
            memcpy(&response[9], bitData, byteCount);
            LeaveCriticalSection(&g_cs);
            send(client, (char*)response, 9 + byteCount, 0);
        }
        // 功能码 06：写单个寄存器
        else if (func == 0x06) {
            uint16_t addr = (req[8] << 8) | req[9];
            uint16_t value = (req[10] << 8) | req[11];
            if (addr >= holding_size) {
                Logger_Write("Modbus请求异常: 从站ID=%d, 写寄存器地址=%d 超出范围 (最大=%d)",
                    slaveID, addr, holding_size - 1);
                LeaveCriticalSection(&g_cs);
                SendException(client, transID, func, 0x02);
                continue;
            }
            holding_ptr[addr] = value;
            LeaveCriticalSection(&g_cs);
            send(client, (char*)req, len, 0);
        }
        // 功能码 10：写多个寄存器
        else if (func == 0x10) {
            uint16_t startAddr = (req[8] << 8) | req[9];
            uint16_t quantity = (req[10] << 8) | req[11];
            uint8_t byteCount = req[12];
            if (quantity < 1 || quantity > 123 ||
                startAddr + quantity > holding_size ||
                byteCount != quantity * 2) {
                Logger_Write("Modbus请求异常: 从站ID=%d, 写多个寄存器 起始=%d 数量=%d, 超出范围或长度不匹配 (最大=%d)",
                    slaveID, startAddr, quantity, holding_size - 1);
                LeaveCriticalSection(&g_cs);
                SendException(client, transID, func, 0x02);
                continue;
            }
            for (uint16_t i = 0; i < quantity; i++) {
                holding_ptr[startAddr + i] = (req[13 + i * 2] << 8) | req[14 + i * 2];
            }
            LeaveCriticalSection(&g_cs);
            uint8_t resp[12];
            resp[0] = transID >> 8; resp[1] = transID & 0xFF;
            resp[2] = 0; resp[3] = 0;
            resp[4] = 0; resp[5] = 6;
            resp[6] = slaveID;
            resp[7] = func;
            resp[8] = startAddr >> 8; resp[9] = startAddr & 0xFF;
            resp[10] = quantity >> 8; resp[11] = quantity & 0xFF;
            send(client, (char*)resp, 12, 0);
        }
        else {
            Logger_Write("Modbus请求异常: 不支持的功码=0x%02X", func);
            LeaveCriticalSection(&g_cs);
            SendException(client, transID, func, 0x01);
        }
    }

    closesocket(client);
}

unsigned __stdcall ModbusListenThread(void* param) {
    struct sockaddr_in addr;
    int wsaError;

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        wsaError = WSAGetLastError();
        Logger_Write("Modbus TCP 监听失败: 无法创建 socket, 错误码=%d", wsaError);
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(g_cfg.tcp_port);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        wsaError = WSAGetLastError();
        Logger_Write("Modbus TCP 监听失败: 端口 %d 绑定失败, 错误码=%d (可能端口被占用或被安全软件阻止)",
            g_cfg.tcp_port, wsaError);
        closesocket(listen_sock);
        return 1;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        wsaError = WSAGetLastError();
        Logger_Write("Modbus TCP 监听失败: 端口 %d 进入监听状态失败, 错误码=%d", g_cfg.tcp_port, wsaError);
        closesocket(listen_sock);
        return 1;
    }

    g_listen_socket = listen_sock;
    Logger_Write("Modbus TCP 服务器已成功启动，监听端口：%d，从站 ID：%d", g_cfg.tcp_port, g_cfg.slave_id);

    while (g_running) {
        struct sockaddr_in client_addr;
        int len = sizeof(client_addr);
        SOCKET client = accept(listen_sock, (struct sockaddr*)&client_addr, &len);
        if (client == INVALID_SOCKET) {
            if (!g_running) break;  // 正常关闭
            wsaError = WSAGetLastError();
            Logger_Write("Modbus TCP 接受客户端连接失败, 错误码=%d", wsaError);
            continue;
        }
        _beginthread(ModbusClientThread, 0, (void*)(intptr_t)client);
    }

    closesocket(listen_sock);
    g_listen_socket = INVALID_SOCKET;
    Logger_Write("Modbus TCP 监听线程退出");
    return 0;
}