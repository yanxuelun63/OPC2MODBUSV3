#pragma warning(disable:28251)
#define _OPCDA_20_
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <initguid.h>
#include <opcda.h>
#include <opcerror.h>
#include "mapping2.h"
#include "logger.h"   // 新增：包含日志模块

const IID IID_IOPCServer = { 0x39c13a4d,0x011e,0x11d0,{0x96,0x75,0x00,0x20,0xaf,0xd8,0xad,0xb3} };
const IID IID_IOPCSyncIO = { 0x39c13a52,0x011e,0x11d0,{0x96,0x75,0x00,0x20,0xaf,0xd8,0xad,0xb3} };
const IID IID_IOPCItemMgt = { 0x39c13a54,0x011e,0x11d0,{0x96,0x75,0x00,0x20,0xaf,0xd8,0xad,0xb3} };

#include "opc_client.h"
#include "config.h"
#pragma comment(lib, "ws2_32.lib")

extern volatile LONG g_running;
extern CRITICAL_SECTION g_cs;
extern HWND g_hMainWnd;

// 将 value 的 bytes 根据 order 字符串重排，输出到 output 数组
void ReorderBytes(uint8_t* output, const uint8_t* input, int count, const char* order) {
    for (int i = 0; i < count; i++) {
        char c = order[i];
        int idx;
        if (c >= 'A' && c <= 'H') idx = c - 'A';
        else if (c >= 'a' && c <= 'h') idx = c - 'a';
        else idx = 0;
        if (idx < count)
            output[i] = input[idx];
        else
            output[i] = 0;
    }
}

// 快速检查目标主机 135 端口是否可达（超时 3 秒）
static BOOL IsHostReachable(const char* ip) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return FALSE;

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(135);
    addr.sin_addr.s_addr = inet_addr(ip);    // 使用 inet_addr

    connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);
    struct timeval timeout = { 3, 0 };

    int selectResult = select(0, NULL, &set, NULL, &timeout);
    closesocket(sock);

    return (selectResult > 0);
}// ==================== OPC 连接 ====================
int OPC_Connect(OPCServerInfo* srv) {
    CLSID clsid;
    HRESULT hr;

    // 1. 优先使用 CLSID 字符串（如果存在）
    if (srv->clsid_str[0] != '\0') {
        wchar_t wclsid[256];
        MultiByteToWideChar(CP_ACP, 0, srv->clsid_str, -1, wclsid, 256);
        hr = CLSIDFromString(wclsid, &clsid);
        if (FAILED(hr)) {
            sprintf(srv->lastError, "CLSID 解析失败");
            if (srv->lastLogResult != 1) {
                Logger_Write("OPC连接失败: 服务器=%s, IP=%s, 错误=%s", srv->name, srv->ip, srv->lastError);
                srv->lastLogResult = 1;
            }
            return -1;
        }
    }
    // 2. 否则使用 ProgID 解析
    else {
        wchar_t wprog[256];
        MultiByteToWideChar(CP_ACP, 0, srv->progID, -1, wprog, 256);
        hr = CLSIDFromProgID(wprog, &clsid);
        if (FAILED(hr)) {
            // 3. 最后尝试将 ProgID 字段直接作为 CLSID 字符串解析（兼容旧格式）
            hr = CLSIDFromString(wprog, &clsid);
            if (FAILED(hr)) {
                sprintf(srv->lastError, "无法解析 ProgID/CLSID");
                if (srv->lastLogResult != 1) {
                    Logger_Write("OPC连接失败: 服务器=%s, IP=%s, 错误=%s", srv->name, srv->ip, srv->lastError);
                    srv->lastLogResult = 1;
                }
                return -1;
            }
        }
    }

    // 远程或本地连接
    if (srv->remote) {
        // ★ 快速预检：如果 135 端口不通，直接返回不可达
        if (!IsHostReachable(srv->ip)) {
            sprintf(srv->lastError, "远程主机不可达 (135 端口不通)");
            if (srv->lastLogResult != 1) {
                Logger_Write("OPC连接失败: 服务器=%s, IP=%s, 错误=%s", srv->name, srv->ip, srv->lastError);
                srv->lastLogResult = 1;
            }
            return -1;
        }
        BOOL connected = FALSE;
        MULTI_QI mqi = { &IID_IOPCServer, NULL, S_OK };

        // ---------- 尝试1：匿名级别（NONE），模拟原行为 ----------
        {
            COAUTHINFO authInfo1 = { 0 };
            authInfo1.dwAuthnSvc = RPC_C_AUTHN_WINNT;
            authInfo1.dwAuthzSvc = RPC_C_AUTHZ_NONE;
            authInfo1.dwAuthnLevel = RPC_C_AUTHN_LEVEL_NONE;   // 强制匿名
            authInfo1.dwImpersonationLevel = RPC_C_IMP_LEVEL_IMPERSONATE;
            authInfo1.pAuthIdentityData = NULL;
            authInfo1.dwCapabilities = EOAC_NONE;

            COSERVERINFO csi1 = { 0 };
            csi1.pwszName = srv->ip_wide;
            csi1.pAuthInfo = &authInfo1;

            hr = CoCreateInstanceEx(&clsid, NULL, CLSCTX_REMOTE_SERVER, &csi1, 1, &mqi);
            if (SUCCEEDED(hr) && SUCCEEDED(mqi.hr)) {
                connected = TRUE;
            }
        }

        // ---------- 尝试2：如果失败且配置了用户名密码，使用带凭据的 CONNECT 级别 ----------
        if (!connected && srv->user[0] != '\0' && srv->pass_encrypted[0] != '\0') {
            // 解密密码（支持 ENC: 前缀）
            char password[256] = { 0 };
            if (strncmp(srv->pass_encrypted, "ENC:", 4) == 0) {
                if (!DecryptPassword(srv->pass_encrypted + 4, password, sizeof(password))) {
                    strcpy(srv->lastError, "OPC服务器密码解密错误，请从通讯设置界面中重新填写服务器信息");
                    if (srv->lastLogResult != 1) {   // 避免重复记录
                        Logger_Write("OPC连接失败: 密码解密错误, 服务器=%s", srv->name);
                        srv->lastLogResult = 1;
                    }
                    return -1;
                }
            }
            else {
                strncpy(password, srv->pass_encrypted, sizeof(password) - 1);
            }

            wchar_t wUser[64] = { 0 }, wPass[256] = { 0 };
            MultiByteToWideChar(CP_ACP, 0, srv->user, -1, wUser, 64);
            MultiByteToWideChar(CP_ACP, 0, password, -1, wPass, 256);

            COAUTHIDENTITY authIdent = { 0 };
            authIdent.User = wUser;
            authIdent.Password = wPass;
            authIdent.Domain = L"";            // 本地账户使用空字符串或 L"."
            authIdent.UserLength = (DWORD)wcslen(wUser);
            authIdent.PasswordLength = (DWORD)wcslen(wPass);
            authIdent.Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;

            COAUTHINFO authInfo2 = { 0 };
            authInfo2.dwAuthnSvc = RPC_C_AUTHN_WINNT;
            authInfo2.dwAuthzSvc = RPC_C_AUTHZ_NONE;
            authInfo2.dwAuthnLevel = RPC_C_AUTHN_LEVEL_CONNECT;  // 需认证
            authInfo2.dwImpersonationLevel = RPC_C_IMP_LEVEL_IMPERSONATE;
            authInfo2.pAuthIdentityData = &authIdent;
            authInfo2.dwCapabilities = EOAC_NONE;

            COSERVERINFO csi2 = { 0 };
            csi2.pwszName = srv->ip_wide;
            csi2.pAuthInfo = &authInfo2;

            hr = CoCreateInstanceEx(&clsid, NULL, CLSCTX_REMOTE_SERVER, &csi2, 1, &mqi);
            if (SUCCEEDED(hr) && SUCCEEDED(mqi.hr)) {
                connected = TRUE;
                srv->lastError[0] = '\0';   // 清除之前的错误信息
            }
        }

        if (!connected) {
            HRESULT errorCode = FAILED(hr) ? hr : mqi.hr;
            if (errorCode == E_ACCESSDENIED)  // 0x80070005
                sprintf(srv->lastError, "远程创建失败 (访问被拒绝，请检查用户名密码或DCOM权限)");
            else if (errorCode == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE))  // 0x800706BA
                sprintf(srv->lastError, "远程创建失败 (RPC服务器不可用，请检查目标主机是否开机及网络连接)");
            else
                sprintf(srv->lastError, "远程创建失败 (0x%08X)", errorCode);

            if (srv->lastLogResult != 1) {
                Logger_Write("OPC连接失败: 服务器=%s, IP=%s, 错误=%s", srv->name, srv->ip, srv->lastError);
                srv->lastLogResult = 1;
            }
            return -1;
        }
        srv->pOPCServer = (IOPCServer*)mqi.pItf;
    }
    else {
        hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
            &IID_IOPCServer, (void**)&srv->pOPCServer);
        if (FAILED(hr)) {
            sprintf(srv->lastError, "创建 IOPCServer 失败 (0x%08X)", hr);
            if (srv->lastLogResult != 1) {
                Logger_Write("OPC连接失败: 服务器=%s, IP=%s, 错误=%s", srv->name, srv->ip, srv->lastError);
                srv->lastLogResult = 1;
            }
            return -1;
        }
    }

    srv->connected = 1;
    srv->lastError[0] = 0;
    srv->reconnectDelay = 10000;
    srv->lastReconnectAttempt = 0;

    // 只在状态改变时记录日志
    if (srv->lastLogResult != 0) {
        Logger_Write("OPC连接成功: 服务器=%s, IP=%s, ProgID=%s", srv->name, srv->ip, srv->progID);
        srv->lastLogResult = 0;
    }
    return 0;
}
int OPC_SetupGroupAndItems(OPCServerInfo* srv) {
    if (!srv->pOPCServer) return -1;

    // 添加 Group，直接获取 IOPCSyncIO
    IOPCSyncIO* pSyncIO = NULL;
    OPCHANDLE hGroup;
    DWORD revisedUpdateRate;
    HRESULT hr = srv->pOPCServer->lpVtbl->AddGroup(srv->pOPCServer,
        L"ModbusGroup", TRUE, g_cfg.refresh_ms, 0,
        NULL, NULL, 0,
        &hGroup, &revisedUpdateRate,
        &IID_IOPCSyncIO, (LPUNKNOWN*)&pSyncIO);
    if (FAILED(hr)) { sprintf(srv->lastError, "AddGroup 失败"); return -1; }
    srv->pSyncIO = pSyncIO;
    srv->hGroup = hGroup;

    // 获取 IOPCItemMgt 接口
    IOPCItemMgt* pItemMgt = NULL;
    hr = pSyncIO->lpVtbl->QueryInterface(pSyncIO, &IID_IOPCItemMgt, (void**)&pItemMgt);
    if (FAILED(hr)) { sprintf(srv->lastError, "获取 IOPCItemMgt 失败"); return -1; }

    // ========== 1. 添加主映射项 (mapping.csv) ==========
    int count = 0;
    for (int i = 0; i < g_mappingCount; i++) {
        if (g_mappings[i].serverIndex == (int)(srv - g_servers)) count++;
    }
    if (count > 0) {
        OPCITEMDEF* pItemDefs = (OPCITEMDEF*)malloc(count * sizeof(OPCITEMDEF));
        int* mapIndices = (int*)malloc(count * sizeof(int));
        memset(pItemDefs, 0, count * sizeof(OPCITEMDEF));

        int idx = 0;
        for (int i = 0; i < g_mappingCount; i++) {
            if (g_mappings[i].serverIndex == (int)(srv - g_servers)) {
                wchar_t wid[256];
                MultiByteToWideChar(CP_ACP, 0, g_mappings[i].itemID, -1, wid, 256);
                pItemDefs[idx].szItemID = wcsdup(wid);
                pItemDefs[idx].bActive = TRUE;
                pItemDefs[idx].hClient = (OPCHANDLE)(i + 1);   // 保留客户端句柄
                pItemDefs[idx].vtRequestedDataType = VT_EMPTY;
                mapIndices[idx] = i;
                idx++;
            }
        }

        OPCITEMRESULT* pAddResults = NULL;
        HRESULT* pErrors = NULL;
        hr = pItemMgt->lpVtbl->AddItems(pItemMgt, count, pItemDefs, &pAddResults, &pErrors);
        if (SUCCEEDED(hr)) {
            for (int i = 0; i < count; i++) {
                int mapIdx = mapIndices[i];
                if (SUCCEEDED(pErrors[i])) {
                    g_mappings[mapIdx].serverHandle = pAddResults[i].hServer;
                }
                else {
                    g_mappings[mapIdx].serverHandle = 0;
                    g_mappings[mapIdx].error = 1;
                    strcpy(g_mappings[mapIdx].opcValue, "ADDFAIL");
                }
            }
        }
        else {
            sprintf(srv->lastError, "AddItems (mapping) 失败");
        }

        for (int i = 0; i < count; i++) free(pItemDefs[i].szItemID);
        free(pItemDefs); free(mapIndices);
        CoTaskMemFree(pAddResults);
        CoTaskMemFree(pErrors);
    }

    // ========== 2. 添加 mapping2 项（特殊应用场景）==========
    if (g_mapping2Enabled) {
        int count2 = 0;
        for (int i = 0; i < g_mapping2Count; i++) {
            if (g_mappings2[i].serverIndex == (int)(srv - g_servers))
                count2++;
        }
        if (count2 > 0) {
            OPCITEMDEF* pItemDefs2 = (OPCITEMDEF*)malloc(count2 * sizeof(OPCITEMDEF));
            if (!pItemDefs2) {
                // 清理已分配资源并返回错误
                free(pItemDefs2);
                return -1;
            }
            int* mapIndices2 = (int*)malloc(count2 * sizeof(int));
            if (!mapIndices2) {
                // 清理已分配资源并返回错误
                free(mapIndices2);
                return -1;
            }
            memset(pItemDefs2, 0, count2 * sizeof(OPCITEMDEF));
            for (int j = 0; j < count2; j++) {
                pItemDefs2[j].szItemID = NULL;
            }
            int idx2 = 0;
            for (int i = 0; i < g_mapping2Count; i++) {
                if (g_mappings2[i].serverIndex == (int)(srv - g_servers)) {
                    if (idx2 >= count2) break;   // 防止意外越界
                    wchar_t wid[256];
                    MultiByteToWideChar(CP_ACP, 0, g_mappings2[i].itemID, -1, wid, 256);
                    pItemDefs2[idx2].szItemID = wcsdup(wid);
                    pItemDefs2[idx2].bActive = TRUE;
                    pItemDefs2[idx2].hClient = 0;                     // 不使用客户端句柄
                    pItemDefs2[idx2].vtRequestedDataType = VT_UI2;    // 请求 Word
                    mapIndices2[idx2] = i;
                    idx2++;
                }
            }

            OPCITEMRESULT* pAddResults2 = NULL;
            HRESULT* pErrors2 = NULL;
            hr = pItemMgt->lpVtbl->AddItems(pItemMgt, count2, pItemDefs2, &pAddResults2, &pErrors2);
            if (SUCCEEDED(hr)) {
                for (int i = 0; i < count2; i++) {
                    int mapIdx = mapIndices2[i];
                    if (SUCCEEDED(pErrors2[i])) {
                        g_mappings2[mapIdx].serverHandle = pAddResults2[i].hServer;
                    }
                    else {
                        g_mappings2[mapIdx].serverHandle = 0;
                    }
                }
            }
            else {
                // 记录日志，但不中断主流程
                Logger_Write("添加 mapping2 项失败，服务器: %s", srv->name);
            }
            #pragma warning(suppress:6001)
            for (int i = 0; i < count2; i++) free(pItemDefs2[i].szItemID);
            free(pItemDefs2); free(mapIndices2);
            CoTaskMemFree(pAddResults2);
            CoTaskMemFree(pErrors2);
        }
    }

    // 释放 IOPCItemMgt 接口
    pItemMgt->lpVtbl->Release(pItemMgt);
    return (SUCCEEDED(hr) || g_mapping2Enabled) ? 0 : -1;   // 如果主映射失败但 mapping2 存在，仍视为部分成功（可根据需要调整）
}
// ==================== 读取数据 ====================
int OPC_ReadAndUpdate(void) {
    for (int srvIdx = 0; srvIdx < g_serverCount; srvIdx++) {
        OPCServerInfo* srv = &g_servers[srvIdx];
        // ========== 自动重连逻辑 ==========
        if (!srv->connected) {
            ULONGLONG now = GetTickCount64();
            if (now - srv->lastReconnectAttempt >= srv->reconnectDelay) {
                srv->lastReconnectAttempt = now;
                // 尝试断开旧连接（清理可能残留的资源）
                OPC_Disconnect(srv);
                // 尝试重新连接
                if (OPC_Connect(srv) == 0) {
                    if (OPC_SetupGroupAndItems(srv) == 0) {
                        // 重连成功，更新时间戳
                        SYSTEMTIME st; GetLocalTime(&st);
                        sprintf(srv->lastTimeStr, "%04d-%02d-%02d %02d:%02d:%02d",
                            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                        srv->lastError[0] = '\0';
                        srv->connected = 1;
                        srv->reconnectDelay = 10000;   // 恢复初始间隔
                        continue;   // 立即进入本周期读取
                    }
                    else {
                        OPC_Disconnect(srv);
                    }
                }
                // 重连失败：指数退避
                if (srv->reconnectDelay < 300000)       // 最大 5 分钟
                    srv->reconnectDelay *= 2;
                if (srv->reconnectDelay > 300000)
                    srv->reconnectDelay = 300000;
            }
            continue;   // 未到重连时间或重连失败，跳过本服务器
        }
        if (!srv->connected || !srv->pSyncIO) continue;

        // 统计有效项数量
        int count = 0;
        for (int i = 0; i < g_mappingCount; i++) {
            if (g_mappings[i].serverIndex == srvIdx && g_mappings[i].serverHandle != 0)
                count++;
        }
        if (count == 0) continue;

        // 构建 OPCHANDLE 数组
        OPCHANDLE* phServer = (OPCHANDLE*)malloc(count * sizeof(OPCHANDLE));
        int* mapIndices = (int*)malloc(count * sizeof(int));
        if (!phServer || !mapIndices) {
            free(phServer);
            free(mapIndices);
            continue;   // 跳过本次读取，下一轮重试
        }
        int idx = 0;
        for (int i = 0; i < g_mappingCount; i++) {
            if (g_mappings[i].serverIndex == srvIdx && g_mappings[i].serverHandle != 0) {
                phServer[idx] = g_mappings[i].serverHandle;
                mapIndices[idx] = i;
                idx++;
            }
        }

        // 调用 IOPCSyncIO::Read（正确原型）
        OPCITEMSTATE* pItemValues = NULL;
        HRESULT* pErrors = NULL;
        HRESULT hr = srv->pSyncIO->lpVtbl->Read(srv->pSyncIO, OPC_DS_CACHE, count, phServer,
            &pItemValues, &pErrors);
        if (SUCCEEDED(hr)) {
            EnterCriticalSection(&g_cs);
            for (int i = 0; i < count; i++) {
                int mapIdx = mapIndices[i];
                MappingItem* p = &g_mappings[mapIdx];
                if (FAILED(pErrors[i])) {
                    p->error = 1; strcpy(p->opcValue, "ERR"); p->lastVt = 0;
                    continue;
                }
                p->lastVt = pItemValues[i].vDataValue.vt;
                p->error = 1; strcpy(p->opcValue, "ERR");

                // 类型转换（与测试程序一致）
                VARIANT* v = &pItemValues[i].vDataValue;
                switch (p->type) {
                case TYPE_BOOL: {
                    if (v->vt == VT_BOOL) {
                        BOOL b = v->boolVal ? 1 : 0;
                        p->rawValue = b;
                        sprintf(p->opcValue, "%d", b);
                        p->error = 0;
                        if (p->modbusAddr < g_coilsSize) g_coils[p->modbusAddr] = b;
                    }
                    break;
                }

                case TYPE_WORD: {
                    uint16_t val = 0; int ok = 0;
                    if (v->vt == VT_UI2) { val = v->uiVal; ok = 1; }
                    else if (v->vt == VT_I2) { val = (uint16_t)v->iVal; ok = 1; }
                    if (ok) {
                        p->rawValue = val;
                        sprintf(p->opcValue, "%u", val);
                        p->error = 0;
                        if (p->modbusAddr < g_holdingRegsSize) g_holdingRegs[p->modbusAddr] = val;
                    }
                    break;
                }

                case TYPE_INT: {
                    int16_t val = 0; int ok = 0;
                    if (v->vt == VT_I2) { val = v->iVal; ok = 1; }
                    else if (v->vt == VT_UI2) { val = (int16_t)v->uiVal; ok = 1; }
                    if (ok) {
                        p->rawValue = (uint16_t)val;   // 保留二进制位
                        sprintf(p->opcValue, "%d", val);
                        p->error = 0;
                        if (p->modbusAddr < g_holdingRegsSize) g_holdingRegs[p->modbusAddr] = (uint16_t)val;
                    }
                    break;
                }

                case TYPE_DWORD: {
                    uint32_t val = 0; int ok = 0;
                    if (v->vt == VT_UI4) { val = v->ulVal; ok = 1; }
                    else if (v->vt == VT_I4) { val = (uint32_t)v->lVal; ok = 1; }
                    if (ok) {
                        p->rawValue = val;
                        sprintf(p->opcValue, "%u", val);
                        p->error = 0;
                        uint8_t input[4], output[4];
                        input[0] = (val >> 24) & 0xFF;  // A
                        input[1] = (val >> 16) & 0xFF;  // B
                        input[2] = (val >> 8) & 0xFF;   // C
                        input[3] = val & 0xFF;          // D
                        const char* order = p->byteOrder;
                        if (!order[0]) order = "ABCD";
                        ReorderBytes(output, input, 4, order);
                        uint16_t reg0 = (output[0] << 8) | output[1];
                        uint16_t reg1 = (output[2] << 8) | output[3];
                        int a = p->modbusAddr;
                        if (a < g_holdingRegsSize) g_holdingRegs[a] = reg0;
                        if (a + 1 < g_holdingRegsSize) g_holdingRegs[a + 1] = reg1;
                    }
                    break;
                }

                case TYPE_LONG: {
                    int32_t val = 0; int ok = 0;
                    if (v->vt == VT_I4) { val = v->lVal; ok = 1; }
                    else if (v->vt == VT_UI4) { val = (int32_t)v->ulVal; ok = 1; }
                    if (ok) {
                        p->rawValue = (uint32_t)val;
                        sprintf(p->opcValue, "%d", val);
                        p->error = 0;
                        uint8_t input[4], output[4];
                        input[0] = (val >> 24) & 0xFF;  // A
                        input[1] = (val >> 16) & 0xFF;  // B
                        input[2] = (val >> 8) & 0xFF;   // C
                        input[3] = val & 0xFF;          // D
                        const char* order = p->byteOrder;
                        if (!order[0]) order = "ABCD";
                        ReorderBytes(output, input, 4, order);
                        uint16_t reg0 = (output[0] << 8) | output[1];
                        uint16_t reg1 = (output[2] << 8) | output[3];
                        int a = p->modbusAddr;
                        if (a < g_holdingRegsSize) g_holdingRegs[a] = reg0;
                        if (a + 1 < g_holdingRegsSize) g_holdingRegs[a + 1] = reg1;
                    }
                    break;
                }

                case TYPE_FLOAT: {
                    float f = 0.0f; int ok = 0;
                    if (v->vt == VT_R4) { f = v->fltVal; ok = 1; }
                    else if (v->vt == VT_R8) { f = (float)v->dblVal; ok = 1; }
                    if (ok) {
                        memcpy(&p->rawValue, &f, sizeof(f));
                        sprintf(p->opcValue, "%g", f);
                        p->error = 0;
                        uint32_t val = *(uint32_t*)&f;
                        uint8_t input[4], output[4];
                        input[0] = (val >> 24) & 0xFF;  // A
                        input[1] = (val >> 16) & 0xFF;  // B
                        input[2] = (val >> 8) & 0xFF;   // C
                        input[3] = val & 0xFF;          // D
                        const char* order = p->byteOrder;
                        if (!order[0]) order = "ABCD";
                        ReorderBytes(output, input, 4, order);
                        uint16_t reg0 = (output[0] << 8) | output[1];
                        uint16_t reg1 = (output[2] << 8) | output[3];
                        int a = p->modbusAddr;
                        if (a < g_holdingRegsSize) g_holdingRegs[a] = reg0;
                        if (a + 1 < g_holdingRegsSize) g_holdingRegs[a + 1] = reg1;
                    }
                    break;
                }

                case TYPE_DOUBLE: {
                    double d = 0.0; int ok = 0;
                    if (v->vt == VT_R8) { d = v->dblVal; ok = 1; }
                    else if (v->vt == VT_R4) { d = (double)v->fltVal; ok = 1; }
                    if (ok) {
                        memcpy(&p->rawValue, &d, sizeof(d));
                        sprintf(p->opcValue, "%g", d);
                        p->error = 0;
                        uint64_t val = *(uint64_t*)&d;
                        uint8_t input[8], output[8];
                        input[0] = (val >> 56) & 0xFF;
                        input[1] = (val >> 48) & 0xFF;
                        input[2] = (val >> 40) & 0xFF;
                        input[3] = (val >> 32) & 0xFF;
                        input[4] = (val >> 24) & 0xFF;
                        input[5] = (val >> 16) & 0xFF;
                        input[6] = (val >> 8) & 0xFF;
                        input[7] = val & 0xFF;
                        const char* order = p->byteOrder;
                        if (!order[0]) order = "ABCDEFGH";
                        ReorderBytes(output, input, 8, order);
                        uint16_t reg0 = (output[0] << 8) | output[1];
                        uint16_t reg1 = (output[2] << 8) | output[3];
                        uint16_t reg2 = (output[4] << 8) | output[5];
                        uint16_t reg3 = (output[6] << 8) | output[7];
                        int a = p->modbusAddr;
                        if (a < g_holdingRegsSize) g_holdingRegs[a] = reg0;
                        if (a + 1 < g_holdingRegsSize) g_holdingRegs[a + 1] = reg1;
                        if (a + 2 < g_holdingRegsSize) g_holdingRegs[a + 2] = reg2;
                        if (a + 3 < g_holdingRegsSize) g_holdingRegs[a + 3] = reg3;
                    }
                    break;
                }
                }
            }
            LeaveCriticalSection(&g_cs);

            // 更新时间戳
            SYSTEMTIME st; GetLocalTime(&st);
            sprintf(srv->lastTimeStr, "%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
        else {
            sprintf(srv->lastError, "Read 失败 0x%08X", hr);
            srv->connected = 0;                    // 标记为断开，触发重连
            srv->lastReconnectAttempt = GetTickCount64();
        }

        // 释放
        if (pItemValues) CoTaskMemFree(pItemValues);
        if (pErrors) CoTaskMemFree(pErrors);
        free(phServer); free(mapIndices);
    }

    // ========== 处理 mapping2（使用预存句柄）==========
    if (g_mapping2Enabled) {
        for (int srvIdx = 0; srvIdx < g_serverCount; srvIdx++) {
            OPCServerInfo* srv = &g_servers[srvIdx];
            if (!srv->connected || !srv->pSyncIO) continue;

            // 收集该服务器上 mapping2 的有效句柄
            int count = 0;
            for (int i = 0; i < g_mapping2Count; i++) {
                if (g_mappings2[i].serverIndex == srvIdx && g_mappings2[i].serverHandle != 0)
                    count++;
            }
            if (count == 0) continue;

            OPCHANDLE* phServer = (OPCHANDLE*)malloc(count * sizeof(OPCHANDLE));
            int* mapIndices = (int*)malloc(count * sizeof(int));
            if (!phServer || !mapIndices) {
                free(phServer);
                free(mapIndices);
                continue;   // 跳过本次读取，下一轮重试
            }
            int idx = 0;
            for (int i = 0; i < g_mapping2Count; i++) {
                if (g_mappings2[i].serverIndex == srvIdx && g_mappings2[i].serverHandle != 0) {
                    phServer[idx] = g_mappings2[i].serverHandle;
                    mapIndices[idx] = i;
                    idx++;
                }
            }

            // 同步读取（使用主读取相同的模式）
            OPCHANDLE* phServerOut = NULL;
            OPCITEMSTATE* pItemValues = NULL;   // 关键：使用 OPCITEMSTATE 结构（与主程序一致）
            HRESULT* pReadErrors = NULL;
            HRESULT hr = srv->pSyncIO->lpVtbl->Read(srv->pSyncIO, OPC_DS_CACHE, count, phServer,
                &pItemValues, &pReadErrors);
            if (SUCCEEDED(hr)) {
                EnterCriticalSection(&g_cs);
                for (int i = 0; i < count; i++) {
                    int mapIdx = mapIndices[i];
                    Mapping2Item* p = &g_mappings2[mapIdx];
                    if (FAILED(pReadErrors[i])) {
                        // 读取失败，置0
                        if (g_coils2 && p->coilsBase + 4 < g_coils2Size) {
                            g_coils2[p->coilsBase + 0] = 0;
                            g_coils2[p->coilsBase + 1] = 0;
                            g_coils2[p->coilsBase + 2] = 0;
                            g_coils2[p->coilsBase + 3] = 0;
                            g_coils2[p->coilsBase + 4] = 0;
                        }
                        if (g_holdingRegs2 && p->modbusAddr + 1 < g_holdingRegs2Size) {
                            g_holdingRegs2[p->modbusAddr] = 0;
                            g_holdingRegs2[p->modbusAddr + 1] = 0;
                        }
                        continue;
                    }

                    // 读取成功，解析
                    if (pItemValues[i].vDataValue.vt == VT_UI2) {
                        uint16_t raw = pItemValues[i].vDataValue.uiVal;
                        if (g_coils2) {
                            g_coils2[p->coilsBase + 0] = (raw >> 15) & 1;
                            g_coils2[p->coilsBase + 1] = (raw >> 14) & 1;
                            g_coils2[p->coilsBase + 2] = (raw >> 13) & 1;
                            g_coils2[p->coilsBase + 3] = (raw >> 12) & 1;
                            g_coils2[p->coilsBase + 4] = 1;   // 通讯正常
                        }
                        // 浮点计算
                        float f = (float)(raw & 0x0FFF) * p->factor;
                        p->computedFloat = f;
                        if (g_holdingRegs2 && p->modbusAddr + 1 < g_holdingRegs2Size) {
                            // 按字节序写入
                            uint32_t v = *(uint32_t*)&f;
                            uint8_t input[4], output[4];
                            input[0] = (v >> 24) & 0xFF;
                            input[1] = (v >> 16) & 0xFF;
                            input[2] = (v >> 8) & 0xFF;
                            input[3] = v & 0xFF;
                            const char* order = p->byteOrder;
                            if (!order[0]) order = "ABCD";
                            ReorderBytes(output, input, 4, order);
                            g_holdingRegs2[p->modbusAddr] = (output[0] << 8) | output[1];
                            g_holdingRegs2[p->modbusAddr + 1] = (output[2] << 8) | output[3];
                        }
                    }
                    else {
                        // 类型不匹配，视为失败
                        if (g_coils2 && p->coilsBase + 4 < g_coils2Size) {
                            g_coils2[p->coilsBase + 4] = 0;
                        }
                    }
                }
                LeaveCriticalSection(&g_cs);
                if (pItemValues) CoTaskMemFree(pItemValues);
                if (pReadErrors) CoTaskMemFree(pReadErrors);
                if (phServerOut) CoTaskMemFree(phServerOut);
            }
            else {
                // 整体读取失败，所有项置0
                EnterCriticalSection(&g_cs);
                for (int i = 0; i < count; i++) {
                    int mapIdx = mapIndices[i];
                    Mapping2Item* p = &g_mappings2[mapIdx];
                    if (g_coils2 && p->coilsBase + 4 < g_coils2Size) {
                        g_coils2[p->coilsBase + 0] = 0;
                        g_coils2[p->coilsBase + 1] = 0;
                        g_coils2[p->coilsBase + 2] = 0;
                        g_coils2[p->coilsBase + 3] = 0;
                        g_coils2[p->coilsBase + 4] = 0;
                    }
                    if (g_holdingRegs2 && p->modbusAddr + 1 < g_holdingRegs2Size) {
                        g_holdingRegs2[p->modbusAddr] = 0;
                        g_holdingRegs2[p->modbusAddr + 1] = 0;
                    }
                }
                LeaveCriticalSection(&g_cs);
            }
            free(phServer);
            free(mapIndices);
        }
    }

    return 0;
}

// ==================== 断开连接 ====================
void OPC_Disconnect(OPCServerInfo* srv) {
    if (srv->connected) {
        if (srv->isTemporary) {
            // 临时连接断开时记录一条更明确的日志，或直接跳过
            Logger_Write("导出临时OPC连接断开: 服务器=%s", srv->name);
        }
        else {
            Logger_Write("OPC断开连接: 服务器=%s", srv->name);
        }
        srv->lastLogResult = -1;
    }
    if (srv->pSyncIO) { srv->pSyncIO->lpVtbl->Release(srv->pSyncIO); srv->pSyncIO = NULL; }
    if (srv->pOPCServer) { srv->pOPCServer->lpVtbl->Release(srv->pOPCServer); srv->pOPCServer = NULL; }
    srv->connected = 0;
}