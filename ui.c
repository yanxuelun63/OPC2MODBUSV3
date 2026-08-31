#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <process.h>
#include <shellapi.h>
#include <commctrl.h>
#include <ole2.h>
#include <objbase.h>

#include "opc_enum.h"      // 提供 EnumRemoteOPCServers 声明
#include "opc_ids.h"       // 资源 ID
#include "config.h"
#include "servers.h"
#include "mapping.h"
#include "opc_client.h"
#include "opc_browse.h"
#include <userenv.h>            // 可能需要，但 GetUserName 在 winbase.h 中
#pragma comment(lib, "advapi32.lib")
// 全局声明
extern void StopGateway(void);          // 停止网关所有线程和连接
extern HANDLE g_hMutex;                 // main.c 中的全局互斥体句柄
extern volatile LONG g_running;
extern CRITICAL_SECTION g_cs;
extern HANDLE g_hOPCThread;
extern HANDLE g_hModbusListenThread;
unsigned __stdcall OPCThread(void* param);   // 声明 OPC 读取线程函数
HWND g_hMainWnd = NULL;

BOOL g_bExporting = FALSE;          // 导出进行中标志
HANDLE g_hExportThread = NULL;      // 导出线程句柄

// 导出线程参数
typedef struct {
    char serverName[64];
    char ip[64];
    char progID[128];
    char clsid[128];
    char user[64];
    char passEnc[1024];
    HWND hWnd;
    HWND hProgressWnd;
} ExportThreadParam;

static HWND g_hHelpDlg = NULL;   // 帮助窗口句柄

static const char* HELP_TEXT =
"========================================\r\n"
" OPC → Modbus TCP 网关 使用帮助\r\n"
"========================================\r\n"
"\r\n"
"【软件功能】\r\n"
"本软件从 OPC DA 服务器读取实时数据，并通过 Modbus TCP 协议转发，\r\n"
"供其他支持 Modbus 的设备或系统采集。支持本地和远程 OPC 服务器连接，\r\n"
"支持多种数据类型、字节序配置、多服务器、多客户端同时访问。\r\n"
"\r\n"
"【基本操作】\r\n"
"1. 启动程序后，首先进入“通讯参数设置”界面。\r\n"
"2. 在此界面可手动添加 OPC 服务器（输入名称、IP、ProgID/CLSID、用户名、密码），\r\n"
"   也可通过“枚举”按钮搜索远程或本地的 OPC 服务器列表并添加。\r\n"
"3. 已添加的服务器会显示在右侧列表中，可随时删除。\r\n"
"4. 设置 Modbus 端口、从站 ID、刷新间隔，并选择映射文件 (mapping.csv)。\r\n"
"5. 点击“保存并启动”，主窗口显示实时数据表格，Modbus TCP 服务器开始监听。\r\n"
"6. 关闭主窗口可最小化到系统托盘，右键托盘图标可显示窗口或退出。\r\n"
"7. 点击主窗口右下角“帮助”按钮可随时查看本文档。\r\n"
"注意：加密按钮可用于程序及servers.csv映射文件转移至其他设备或本机登录用户名变更后, \r\n"
"      重新输入OPC明文使用加密功能获得新密码，将新密码填入原servers.csv替换旧密码。 \r\n"
"\r\n"
"【映射文件 (mapping.csv) 编制要求】\r\n"
"文件格式：CSV (逗号分隔)，ANSI 编码，第一行为表头，之后每行定义一个点位。\r\n"
"表头及列说明：\r\n"
"  ServerName, OPCItemID, DataType, ModbusAddr, ByteOrder\r\n"
"列1: ServerName   - 必须与 servers.csv 中的服务器名称一致。\r\n"
"列2: OPCItemID    - OPC 服务器中的标签路径（如 Channel1.Device1.Tag1）。\r\n"
"列3: DataType     - 数据类型，支持以下值（区分大小写）：\r\n"
"       Bool     → 布尔量，映射到 Modbus 线圈 (0 区)\r\n"
"       Word     → 16 位无符号整数\r\n"
"       Int      → 16 位有符号整数\r\n"
"       DWord    → 32 位无符号整数\r\n"
"       Long     → 32 位有符号整数\r\n"
"       Float    → 32 位单精度浮点数\r\n"
"       Double   → 64 位双精度浮点数\r\n"
"列4: ModbusAddr  - Modbus 起始地址（0-based）。\r\n"
"       对于 Bool 类型，此地址为线圈地址；\r\n"
"       对于 Word/Int 类型，地址占用 1 个寄存器；\r\n"
"       对于 DWord/Long/Float，地址占用 2 个寄存器，仅填写首地址；\r\n"
"       对于 Double，地址占用 4 个寄存器，仅填写首地址。\r\n"
"列5: ByteOrder   - 多字节/多字排列顺序，仅对 DWord/Long/Float/Double 有效。\r\n"
"       Word/Int/Bool 可留空或填任意值。\r\n"
"       支持格式：\r\n"
"         ABCD     → 大端模式（默认），高字节在低地址\r\n"
"         CDAB     → 中间交换\r\n"
"         BADC     → 另一交换\r\n"
"         DCBA     → 小端模式\r\n"
"       对于 Double 类型，类似地使用 8 字母组合，如 ABCDEFGH。\r\n"
"示例：\r\n"
"  Kepware, Channel1.Device1.Tag1, Word, 0, \r\n"
"  Kepware, Channel1.Device1.FloatTag, Float, 2, CDAB\r\n"
"\r\n"
"【OPC 服务器配置 (servers.csv) 编制要求】\r\n"
"文件格式：CSV，ANSI 编码，第一行为表头。\r\n"
"表头及列说明：\r\n"
"  ServerName, IPAddress, ProgID, UserName, Password, CLSID\r\n"
"列1: ServerName   - 自定义服务器名称，用于在 mapping.csv 中引用。\r\n"
"列2: IPAddress    - OPC 服务器的 IP 地址。本地填 127.0.0.1。\r\n"
"列3: ProgID       - OPC 服务器的 ProgID 或 CLSID 字符串。\r\n"
"       本地连接可直接使用 ProgID；远程连接建议使用 CLSID\r\n"
"       （可通过“枚举”按钮获取远程服务器的 ProgID 和 CLSID）。\r\n"
"列4: UserName     - 登录远程计算机的用户名。本地可留空。\r\n"
"列5: Password     - 对应密码，支持明文或 ENC: 开头的加密密文。\r\n"
"列6: CLSID        - 可选，显式指定服务器的 CLSID，优先于 ProgID 使用。\r\n"
"示例：\r\n"
"  LocalKEP, 127.0.0.1, KEPware.KEPServerEx.V4, , , \r\n"
"  RemoteKEP, 192.168.1.100, {7BC0CC8E-482C-47CA-ABDC-0FE7F9C6E729}, admin, ENC:... \r\n"
"\r\n"
"\r\n"
"【特殊应用场景：mapping2.csv】\r\n"
"若程序目录下存在 mapping2.csv 文件且包含数据行，程序将自动启用第二从站\r\n"
"（从站 ID = 主从站 ID + 1），并对指定的 Word 类型标签进行位解析和浮点转发。\r\n"
"\r\n"
"文件格式：CSV，ANSI 编码，第一行为表头，之后每行定义一个解析条目。\r\n"
"表头及列说明：\r\n"
"  ServerName, OPCItemID, ModbusAddr, Factor, ByteOrder\r\n"
"列1: ServerName   - 必须与 servers.csv 中的服务器名称一致。\r\n"
"列2: OPCItemID    - OPC 服务器中的 Word 类型标签路径。\r\n"
"列3: ModbusAddr   - Modbus 保持寄存器首地址（0-based），浮点结果占用 2 个寄存器。\r\n"
"列4: Factor       - 计算因子。bit11-0 的整数值 × Factor 得到最终浮点数。\r\n"
"列5: ByteOrder    - 浮点数的字节序，支持 ABCD（默认）、CDAB、BADC、DCBA。\r\n"
"\r\n"
"解析规则：\r\n"
"  - 读取到的 Word 值的 bit15-12 分别作为 4 个线圈输出，线圈地址为：\r\n"
"        base = ModbusAddr / 2 * 5\r\n"
"        bit15 → 线圈 base+0\r\n"
"        bit14 → 线圈 base+1\r\n"
"        bit13 → 线圈 base+2\r\n"
"        bit12 → 线圈 base+3\r\n"
"  - 通讯状态位位于线圈 base+4：成功读取 OPC 数据时为 1，失败时为 0。\r\n"
"  - bit11-0（0~4095）作为整型，乘以 Factor 后转为浮点数，按 ByteOrder 存入\r\n"
"    保持寄存器 ModbusAddr 和 ModbusAddr+1。\r\n"
"\r\n"
"启用条件：\r\n"
"  - 文件 mapping2.csv 必须存在于程序目录，且至少包含一条有效数据行。\r\n"
"  - 若文件不存在或只有表头，此功能自动禁用，不影响主从站运行。\r\n"
"\r\n"
"【注意事项】\r\n"
"- 映射文件中的标签路径必须与 OPC 服务器中完全一致，建议先用“导出条目”功能获取完整列表。\r\n"
"- 映射文件mapping.csv可通过调出通讯参数设置界面选择文件后热加载执行。\r\n"
"- 映射文件mapping2.csv必须放在程序同一目录且不支持热加载，修改文件后必须重启程序方能执行。\r\n"
"- Modbus 从站 ID 需与客户端设置一致，否则不会响应。通讯端口和ID改变后需重启程序。\r\n"
"- 最大支持64个OPC服务器连接。\r\n"
"- 程序退出时自动断开所有 OPC 连接并释放端口。\r\n";


// 函数声明
void UpdateServerStatus(HWND hEdit);
void UpdateListView(HWND hList);
void UpdateListViewData(HWND hList);
void LoadServersToLocalList(HWND hList);
INT_PTR CALLBACK InputBoxDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
BOOL InputBox(HWND hParent, const char* prompt, char* outBuf, int bufSize);
BOOL VerifyCurrentUserPassword(HWND hParent, const char* password);
BOOL ShowPasswordVerifyDialog(HWND hParent);
INT_PTR CALLBACK PasswordVerifyDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

static DWORD WINAPI ExportThreadProc(LPVOID lpParam) {
    ExportThreadParam* param = (ExportThreadParam*)lpParam;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    OPCServerInfo srv;
    memset(&srv, 0, sizeof(srv));
    strncpy(srv.name, param->serverName, sizeof(srv.name) - 1);
    strncpy(srv.ip, param->ip, sizeof(srv.ip) - 1);
    MultiByteToWideChar(CP_ACP, 0, param->ip, -1, srv.ip_wide, 64);
    strncpy(srv.progID, param->progID, sizeof(srv.progID) - 1);
    strncpy(srv.clsid_str, param->clsid, sizeof(srv.clsid_str) - 1);
    strncpy(srv.user, param->user, sizeof(srv.user) - 1);
    strncpy(srv.pass_encrypted, param->passEnc, sizeof(srv.pass_encrypted) - 1);
    srv.remote = (strcmp(param->ip, "127.0.0.1") != 0 && strcmp(param->ip, "localhost") != 0);
    srv.isTemporary = 1;

    int exported = -1;
    if (OPC_Connect(&srv) == 0) {
        exported = BrowseAndExportItems(&srv, param->hProgressWnd);
        OPC_Disconnect(&srv);
    }

    CoUninitialize();

    if (IsWindow(param->hWnd)) {
        // 先发送最终进度（使用实际导出数），再发送完成消息
        if (exported >= 0) {
            PostMessage(param->hWnd, WM_EXPORT_PROGRESS, (WPARAM)exported, 0);
        }
        PostMessage(param->hWnd, WM_EXPORT_DONE, (WPARAM)(exported >= 0 ? 0 : -1), 0);
    }
    free(param);
    return 0;
}

// 检测端口是否可用（尝试绑定）
static BOOL IsPortAvailable(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return FALSE;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(sock);
    return (result == 0);
}

// ====================== 服务器状态更新 ======================
void UpdateServerStatus(HWND hEdit) {
    char buf[2048] = "";
    for (int i = 0; i < g_serverCount; i++) {
        OPCServerInfo* s = &g_servers[i];
        char line[256];
        if (s->connected) {
            sprintf(line, "[%s] %s : 已连接\r\n", s->name, s->ip);
            strcat(buf, line);
            sprintf(line, "   ProgID: %s  标签数: %d  最后读取: %s\r\n",
                s->progID, s->itemCount, s->lastTimeStr[0] ? s->lastTimeStr : "未读取");
            strcat(buf, line);
            if (s->lastError[0]) { sprintf(line, "   错误: %s\r\n", s->lastError); strcat(buf, line); }
        }
        else {
            sprintf(line, "[%s] %s : 断开\r\n", s->name, s->ip); strcat(buf, line);
            if (s->lastError[0]) { sprintf(line, "   错误: %s\r\n", s->lastError); strcat(buf, line); }
            if (s->lastTimeStr[0]) {
                sprintf(line, "   最后读取: %s\r\n", s->lastTimeStr);
                strcat(buf, line);
            }
        }
        strcat(buf, "\r\n");
    }
    SetWindowText(hEdit, buf);
}

// ====================== 表格更新 ======================
void UpdateListView(HWND hList) {
    ListView_DeleteAllItems(hList);
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_mappingCount; i++) {
        MappingItem* p = &g_mappings[i];
        LVITEM lvi = { 0 }; char buf[64];
        lvi.mask = LVIF_TEXT; lvi.iItem = i; lvi.iSubItem = 0;
        lvi.pszText = g_servers[p->serverIndex].name;
        ListView_InsertItem(hList, &lvi);

        ListView_SetItemText(hList, i, 1, p->itemID);
        static const char* tnames[] = { "Bool","Word","Int","DWord","Long","Float","Double" };
        ListView_SetItemText(hList, i, 2, (char*)tnames[p->type]);
        ListView_SetItemText(hList, i, 3, p->opcValue[0] ? p->opcValue : "?");

        char opcHex[32] = "";
        if (!p->error) {
            if (p->type == TYPE_BOOL)       sprintf(opcHex, "0x%02X", (uint8_t)p->rawValue);
            else if (p->type == TYPE_WORD)  sprintf(opcHex, "0x%04X", (uint16_t)p->rawValue);
            else if (p->type == TYPE_INT)   sprintf(opcHex, "0x%04X", (uint16_t)p->rawValue);
            else if (p->type == TYPE_DWORD) sprintf(opcHex, "0x%08X", (uint32_t)p->rawValue);
            else if (p->type == TYPE_LONG)  sprintf(opcHex, "0x%08X", (uint32_t)p->rawValue);
            else if (p->type == TYPE_FLOAT) sprintf(opcHex, "0x%08X", (uint32_t)p->rawValue);
            else if (p->type == TYPE_DOUBLE) sprintf(opcHex, "0x%016llX", (uint64_t)p->rawValue);
        }
        else strcpy(opcHex, "ERR");
        ListView_SetItemText(hList, i, 4, opcHex);

        sprintf(buf, "%d", p->modbusAddr);
        ListView_SetItemText(hList, i, 5, buf);
        ListView_SetItemText(hList, i, 6, p->byteOrder[0] ? p->byteOrder : "-");

        char modbusHex[32] = "";
        if (p->error) {
            strcpy(modbusHex, "ERR");
        }
        else {
            if (p->type == TYPE_BOOL) {
                if (p->modbusAddr < g_coilsSize)
                    sprintf(modbusHex, "0x%02X", g_coils[p->modbusAddr]);
                else strcpy(modbusHex, "ERR");
            }
            else if (p->type == TYPE_WORD || p->type == TYPE_INT) {
                if (p->modbusAddr < g_holdingRegsSize)
                    sprintf(modbusHex, "0x%04X", g_holdingRegs[p->modbusAddr]);
                else strcpy(modbusHex, "ERR");
            }
            else if (p->type == TYPE_DWORD || p->type == TYPE_LONG || p->type == TYPE_FLOAT) {
                if (p->modbusAddr + 1 < g_holdingRegsSize) {
                    uint16_t w0 = g_holdingRegs[p->modbusAddr];
                    uint16_t w1 = g_holdingRegs[p->modbusAddr + 1];
                    uint32_t v = ((uint32_t)w0 << 16) | w1;
                    sprintf(modbusHex, "0x%08X", v);
                }
                else strcpy(modbusHex, "ERR");
            }
            else if (p->type == TYPE_DOUBLE) {
                if (p->modbusAddr + 3 < g_holdingRegsSize) {
                    uint16_t w0 = g_holdingRegs[p->modbusAddr];
                    uint16_t w1 = g_holdingRegs[p->modbusAddr + 1];
                    uint16_t w2 = g_holdingRegs[p->modbusAddr + 2];
                    uint16_t w3 = g_holdingRegs[p->modbusAddr + 3];
                    uint64_t v = ((uint64_t)w0 << 48) | ((uint64_t)w1 << 32) | ((uint64_t)w2 << 16) | w3;
                    sprintf(modbusHex, "0x%016llX", v);
                }
                else strcpy(modbusHex, "ERR");
            }
        }
        ListView_SetItemText(hList, i, 7, modbusHex);
        sprintf(buf, "%d", p->lastVt);
        ListView_SetItemText(hList, i, 8, buf);
    }
    LeaveCriticalSection(&g_cs);
}
// 仅更新数据列（不清除项，保持滚动位置）
void UpdateListViewData(HWND hList) {
    int itemCount = ListView_GetItemCount(hList);
    EnterCriticalSection(&g_cs);

    // 如果项数不匹配（比如热加载后），交给全量刷新处理
    if (itemCount != g_mappingCount) {
        LeaveCriticalSection(&g_cs);
        UpdateListView(hList);   // 全量重建
        return;
    }

    for (int i = 0; i < g_mappingCount; i++) {
        MappingItem* p = &g_mappings[i];

        // 更新第3列：OPC 值
        ListView_SetItemText(hList, i, 3, p->opcValue[0] ? p->opcValue : "?");

        // 更新第4列：OPC HEX
        char opcHex[32] = "";
        if (!p->error) {
            if (p->type == TYPE_BOOL)       sprintf(opcHex, "0x%02X", (uint8_t)p->rawValue);
            else if (p->type == TYPE_WORD)  sprintf(opcHex, "0x%04X", (uint16_t)p->rawValue);
            else if (p->type == TYPE_INT)   sprintf(opcHex, "0x%04X", (uint16_t)p->rawValue);
            else if (p->type == TYPE_DWORD) sprintf(opcHex, "0x%08X", (uint32_t)p->rawValue);
            else if (p->type == TYPE_LONG)  sprintf(opcHex, "0x%08X", (uint32_t)p->rawValue);
            else if (p->type == TYPE_FLOAT) sprintf(opcHex, "0x%08X", (uint32_t)p->rawValue);
            else if (p->type == TYPE_DOUBLE) sprintf(opcHex, "0x%016llX", (uint64_t)p->rawValue);
        }
        else {
            strcpy(opcHex, "ERR");
        }
        ListView_SetItemText(hList, i, 4, opcHex);

        // 更新第7列：Modbus HEX
        char modbusHex[32] = "";
        if (p->error) {
            strcpy(modbusHex, "ERR");
        }
        else {
            if (p->type == TYPE_BOOL) {
                if (p->modbusAddr < g_coilsSize)
                    sprintf(modbusHex, "0x%02X", g_coils[p->modbusAddr]);
                else strcpy(modbusHex, "ERR");
            }
            else if (p->type == TYPE_WORD || p->type == TYPE_INT) {
                if (p->modbusAddr < g_holdingRegsSize)
                    sprintf(modbusHex, "0x%04X", g_holdingRegs[p->modbusAddr]);
                else strcpy(modbusHex, "ERR");
            }
            else if (p->type == TYPE_DWORD || p->type == TYPE_LONG || p->type == TYPE_FLOAT) {
                if (p->modbusAddr + 1 < g_holdingRegsSize) {
                    uint16_t w0 = g_holdingRegs[p->modbusAddr];
                    uint16_t w1 = g_holdingRegs[p->modbusAddr + 1];
                    uint32_t v = ((uint32_t)w0 << 16) | w1;
                    sprintf(modbusHex, "0x%08X", v);
                }
                else strcpy(modbusHex, "ERR");
            }
            else if (p->type == TYPE_DOUBLE) {
                if (p->modbusAddr + 3 < g_holdingRegsSize) {
                    uint16_t w0 = g_holdingRegs[p->modbusAddr];
                    uint16_t w1 = g_holdingRegs[p->modbusAddr + 1];
                    uint16_t w2 = g_holdingRegs[p->modbusAddr + 2];
                    uint16_t w3 = g_holdingRegs[p->modbusAddr + 3];
                    uint64_t v = ((uint64_t)w0 << 48) | ((uint64_t)w1 << 32) | ((uint64_t)w2 << 16) | w3;
                    sprintf(modbusHex, "0x%016llX", v);
                }
                else strcpy(modbusHex, "ERR");
            }
        }
        ListView_SetItemText(hList, i, 7, modbusHex);

        // 更新第8列：VT（变量类型）
        char vtBuf[16];
        sprintf(vtBuf, "%d", p->lastVt);
        ListView_SetItemText(hList, i, 8, vtBuf);
    }
    LeaveCriticalSection(&g_cs);
}

// ====================== 密码输入框 ======================
static char g_inputText[256] = "";

INT_PTR CALLBACK InputBoxDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_EDIT_INPUT, g_inputText);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemText(hDlg, IDC_EDIT_INPUT, g_inputText, sizeof(g_inputText));
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL InputBox(HWND hParent, const char* prompt, char* outBuf, int bufSize) {
    g_inputText[0] = '\0';           // 更安全的清空方式
    INT_PTR ret = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_INPUTBOX), hParent, InputBoxDlgProc);
    if (ret == IDOK) {
        snprintf(outBuf, bufSize, "%s", g_inputText);  // 自动截断并终止
        return TRUE;
    }
    return FALSE;
}

// ====================== OPC服务器列表加载 ======================
void LoadServersToLocalList(HWND hList) {
    ListView_DeleteAllItems(hList);
    for (int i = 0; i < g_serverCount; i++) {
        LVITEM lvi = { 0 };
        lvi.mask = LVIF_TEXT;
        lvi.iItem = 9999;
        lvi.pszText = g_servers[i].name;
        int idx = ListView_InsertItem(hList, &lvi);
        ListView_SetItemText(hList, idx, 1, g_servers[i].ip);
        ListView_SetItemText(hList, idx, 2, g_servers[i].progID);
    }
}

// ====================== 配置对话框 ======================
INT_PTR CALLBACK ConfigDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN_ICON));
        SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        HWND hExportStatus = GetDlgItem(hDlg, IDC_EXPORT_STATUS);
        ShowWindow(hExportStatus, SW_HIDE);
        LoadServers("servers.csv");
        SetDlgItemInt(hDlg, IDC_EDIT_PORT, g_cfg.tcp_port, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_SLAVEID, g_cfg.slave_id, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_INTERVAL, g_cfg.refresh_ms, FALSE);
        SetDlgItemText(hDlg, IDC_EDIT_MAPPING, g_cfg.mapping_path);
        CheckDlgButton(hDlg, IDC_AUTO_START, g_cfg.autoStart ? BST_CHECKED : BST_UNCHECKED);
        // 待配置OPC服务器列表（左）
        HWND hRemote = GetDlgItem(hDlg, IDC_LIST_REMOTE);
        LVCOLUMN lvc = { 0 };
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.cx = 150; lvc.pszText = "ProgID";
        ListView_InsertColumn(hRemote, 0, &lvc);
        lvc.cx = 120;   lvc.pszText = "CLSID";
        ListView_InsertColumn(hRemote, 1, &lvc);
        ListView_SetExtendedListViewStyle(hRemote, LVS_EX_FULLROWSELECT);

        // 已配置OPC服务器列表（右）
        HWND hLocal = GetDlgItem(hDlg, IDC_LIST_LOCAL);
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        lvc.cx = 100; lvc.pszText = "名称";
        ListView_InsertColumn(hLocal, 0, &lvc);
        lvc.cx = 110; lvc.pszText = "IP";
        ListView_InsertColumn(hLocal, 1, &lvc);
        lvc.cx = 180; lvc.pszText = "ProgID";
        ListView_InsertColumn(hLocal, 2, &lvc);
        ListView_SetExtendedListViewStyle(hLocal, LVS_EX_FULLROWSELECT);
        SetWindowLong(hLocal, GWL_STYLE, GetWindowLong(hLocal, GWL_STYLE) | LVS_SHOWSELALWAYS);
        LoadServersToLocalList(hLocal);
        // 居中对话框
        RECT rcDlg;
        GetWindowRect(hDlg, &rcDlg);
        int cxScreen = GetSystemMetrics(SM_CXSCREEN);
        int cyScreen = GetSystemMetrics(SM_CYSCREEN);
        int x = (cxScreen - (rcDlg.right - rcDlg.left)) / 2;
        int y = (cyScreen - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        // 设置默认值
        SetDlgItemText(hDlg, IDC_EDIT_SERVERNAME, "LocalOPC");
        SetDlgItemText(hDlg, IDC_EDIT_SERVERIP, "127.0.0.1");

        return TRUE;
    }

    case WM_EXPORT_DONE: {
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_EXPORT), TRUE);
        g_bExporting = FALSE;
        if (g_hExportThread) {
            CloseHandle(g_hExportThread);
            g_hExportThread = NULL;
        }
        int result = (int)wParam;
        if (result == 0) {
            MessageBox(hDlg, "导出成功！文件已保存到程序目录。", "提示", MB_OK);
            ShowWindow(GetDlgItem(hDlg, IDC_EXPORT_STATUS), SW_HIDE);
        }
        else {
            MessageBox(hDlg, "导出失败，请检查服务器连接或浏览接口。", "错误", MB_ICONERROR);
            ShowWindow(GetDlgItem(hDlg, IDC_EXPORT_STATUS), SW_HIDE);
        }
        return TRUE;
    }

    case WM_EXPORT_PROGRESS: {
        int count = (int)wParam;
        char text[64];
        sprintf(text, "正在导出，已导出 %d 条", count);
        SetDlgItemText(hDlg, IDC_EXPORT_STATUS, text);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_BROWSE: {
            OPENFILENAME ofn = { 0 }; char file[MAX_PATH] = "";
            ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hDlg;
            ofn.lpstrFilter = "CSV Files\0*.csv\0All Files\0*.*\0";
            ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileName(&ofn)) SetDlgItemText(hDlg, IDC_EDIT_MAPPING, file);
            break;
        }
        case IDC_BTN_BROWSE_REMOTE: {
            char ip[64], user[64], pass[64];
            GetDlgItemText(hDlg, IDC_EDIT_SERVERIP, ip, sizeof(ip));
            GetDlgItemText(hDlg, IDC_EDIT_USERNAME, user, sizeof(user));
            GetDlgItemText(hDlg, IDC_EDIT_PASSWORD, pass, sizeof(pass));
            EnumRemoteOPCServers(GetDlgItem(hDlg, IDC_LIST_REMOTE), ip, user, pass);
            break;
        }
        case IDC_BTN_ADD: {
            char name[64] = "", ip[64] = "", progID[256] = "", user[64] = "", pass[64] = "", clsid[256] = "";
            GetDlgItemText(hDlg, IDC_EDIT_SERVERNAME, name, sizeof(name));
            GetDlgItemText(hDlg, IDC_EDIT_SERVERIP, ip, sizeof(ip));
            GetDlgItemText(hDlg, IDC_EDIT_USERNAME, user, sizeof(user));
            GetDlgItemText(hDlg, IDC_EDIT_PASSWORD, pass, sizeof(pass));

           BOOL isRemote = (ip[0] && strcmp(ip, "127.0.0.1") != 0);
            if (isRemote && (user[0] == '\0' || pass[0] == '\0')) {
                MessageBox(hDlg, "远程服务器必须输入用户名和密码！", "错误", MB_ICONERROR);
                break;
            }
         

            // 优先从左侧枚举列表获取选中的 ProgID 和 CLSID
            HWND hRemote = GetDlgItem(hDlg, IDC_LIST_REMOTE);
            int sel = ListView_GetNextItem(hRemote, -1, LVNI_SELECTED);
            if (sel != -1) {
                ListView_GetItemText(hRemote, sel, 0, progID, sizeof(progID));   // ProgID
                ListView_GetItemText(hRemote, sel, 1, clsid, sizeof(clsid));     // CLSID
            }
            else {
                // 没有选中枚举项，从手动输入框获取 ProgID
                GetDlgItemText(hDlg, IDC_EDIT_PROGID, progID, sizeof(progID));
            }

            if (name[0] == '\0' || ip[0] == '\0' || (progID[0] == '\0' && clsid[0] == '\0')) {
                MessageBox(hDlg, "名称、IP 和 ProgID/CLSID 不能为空", "错误", MB_ICONERROR);
                break;
            }

            char encrypted[1024] = "";
            if (pass[0]) {
                char rawEnc[1024];
                if (EncryptPassword(pass, rawEnc, sizeof(rawEnc))) {
                    // 成功加密，添加 ENC: 前缀
                    snprintf(encrypted, sizeof(encrypted), "ENC:%s", rawEnc);
                }
                else {
                    MessageBox(hDlg, "密码加密失败，请重试。", "错误", MB_ICONERROR);
                    break;
                }
            }

            FILE* fp = fopen("servers.csv", "a");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                if (ftell(fp) == 0) {
                    fprintf(fp, "ServerName,IPAddress,ProgID,UserName,Password,CLSID\n");
                }
                fprintf(fp, "%s,%s,%s,%s,%s,%s\n", name, ip, progID, user, encrypted, clsid);
                fclose(fp);
            }

            LoadServers("servers.csv");
            LoadServersToLocalList(GetDlgItem(hDlg, IDC_LIST_LOCAL));
            break;
        }

        case IDC_BTN_DELETE: {
            HWND hLocal = GetDlgItem(hDlg, IDC_LIST_LOCAL);
            int sel = ListView_GetNextItem(hLocal, -1, LVNI_SELECTED);
            if (sel == -1) break;

            char name[64];
            ListView_GetItemText(hLocal, sel, 0, name, sizeof(name));

            FILE* fp = fopen("servers.csv", "w");
            if (fp) {
                fprintf(fp, "ServerName,IPAddress,ProgID,UserName,Password,CLSID\n");
                for (int i = 0; i < g_serverCount; i++) {
                    if (strcmp(g_servers[i].name, name) != 0) {
                        fprintf(fp, "%s,%s,%s,%s,%s,%s\n",
                            g_servers[i].name,
                            g_servers[i].ip,
                            g_servers[i].progID,
                            g_servers[i].user,
                            g_servers[i].pass_encrypted,
                            g_servers[i].clsid_str);
                    }
                }
                fclose(fp);
            }

            LoadServers("servers.csv");
            LoadServersToLocalList(GetDlgItem(hDlg, IDC_LIST_LOCAL));
            break;
        }
        case IDC_BTN_EXPORT: {
            if (g_bExporting) {
                MessageBox(hDlg, "正在导出中，请等待完成。", "提示", MB_OK);
                break;
            }
            HWND hLocal = GetDlgItem(hDlg, IDC_LIST_LOCAL);
            int sel = ListView_GetNextItem(hLocal, -1, LVNI_SELECTED);
            if (sel == -1) {
                MessageBox(hDlg, "请先选择右侧列表中的服务器。", "提示", MB_OK);
                break;
            }
            char name[64];
            ListView_GetItemText(hLocal, sel, 0, name, sizeof(name));

            OPCServerInfo* pSrv = NULL;
            for (int i = 0; i < g_serverCount; i++) {
                if (strcmp(g_servers[i].name, name) == 0) {
                    pSrv = &g_servers[i];
                    break;
                }
            }
            if (!pSrv) break;

            // 禁用导出按钮，防止重复点击
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_EXPORT), FALSE);
            g_bExporting = TRUE;

            ExportThreadParam* param = (ExportThreadParam*)malloc(sizeof(ExportThreadParam));
            if (!param) {
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_EXPORT), TRUE);
                g_bExporting = FALSE;
                break;
            }
            memset(param, 0, sizeof(ExportThreadParam));
            strncpy(param->serverName, pSrv->name, sizeof(param->serverName) - 1);
            strncpy(param->ip, pSrv->ip, sizeof(param->ip) - 1);
            strncpy(param->progID, pSrv->progID, sizeof(param->progID) - 1);
            strncpy(param->clsid, pSrv->clsid_str, sizeof(param->clsid) - 1);
            strncpy(param->user, pSrv->user, sizeof(param->user) - 1);
            strncpy(param->passEnc, pSrv->pass_encrypted, sizeof(param->passEnc) - 1);
            param->hWnd = hDlg;
            param->hProgressWnd = hDlg;

            HWND hExportStatus = GetDlgItem(hDlg, IDC_EXPORT_STATUS);
            ShowWindow(hExportStatus, SW_SHOW);
            SetWindowText(hExportStatus, "正在导出，已导出 0 条");

            g_hExportThread = CreateThread(NULL, 0, ExportThreadProc, param, 0, NULL);
            if (!g_hExportThread) {
                free(param);
                EnableWindow(GetDlgItem(hDlg, IDC_BTN_EXPORT), TRUE);
                ShowWindow(GetDlgItem(hDlg, IDC_EXPORT_STATUS), SW_HIDE);
                g_bExporting = FALSE;
            }
            break;
        }
        case IDC_BTN_ENCRYPT: {
            char plain[256] = "";
            if (InputBox(hDlg, "请输入明文密码:", plain, sizeof(plain))) {
                char enc[1024] = "ENC:";
                if (EncryptPassword(plain, enc + 4, sizeof(enc) - 4)) {
                    // 复制到剪贴板
                    if (OpenClipboard(hDlg)) {
                        EmptyClipboard();
                        int len = (int)strlen(enc) + 1;
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
                        if (hMem) {
                            memcpy(GlobalLock(hMem), enc, len);
                            GlobalUnlock(hMem);
                            SetClipboardData(CF_TEXT, hMem);
                        }
                        CloseClipboard();
                    }
                    MessageBox(hDlg, enc, "加密成功（已复制到剪贴板，请粘贴到CSV密码列）", MB_OK);
                }
                else {
                    MessageBox(hDlg, "加密失败", "错误", MB_ICONERROR);
                }
            }
            break;
        }
        case IDOK: {
            if (g_bExporting) {
                MessageBox(hDlg, "正在导出条目，请等待完成后再操作。", "提示", MB_OK);
                return TRUE;
            }
            int port = GetDlgItemInt(hDlg, IDC_EDIT_PORT, NULL, FALSE);
            if (port < 1 || port > 65535) {
                MessageBox(hDlg, "端口号必须在 1-65535 之间。", "错误", MB_ICONERROR);
                break;
            }

            // 如果主窗口已存在（即程序已运行），跳过端口检测并提示重启生效
            BOOL bRunning = (g_hMainWnd != NULL);

            if (!bRunning) {
                // 首次启动，必须确保端口可用
                if (!IsPortAvailable(port)) {
                    MessageBox(hDlg, "Modbus TCP 端口已被占用，请更换端口。", "端口冲突", MB_ICONERROR);
                    break;
                }
            }

            // 保存配置
            g_cfg.tcp_port = port;
            g_cfg.slave_id = GetDlgItemInt(hDlg, IDC_EDIT_SLAVEID, NULL, FALSE);
            g_cfg.refresh_ms = GetDlgItemInt(hDlg, IDC_EDIT_INTERVAL, NULL, FALSE);
            GetDlgItemText(hDlg, IDC_EDIT_MAPPING, g_cfg.mapping_path, MAX_PATH);
            g_cfg.autoStart = (IsDlgButtonChecked(hDlg, IDC_AUTO_START) == BST_CHECKED) ? 1 : 0;
            SaveConfig(&g_cfg);

            if (bRunning) {
                MessageBox(hDlg, "配置已保存，部分设置将在程序重启后生效。", "提示", MB_OK);
            }

            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            if (g_bExporting) {
                MessageBox(hDlg, "正在导出条目，请等待完成后再操作。", "提示", MB_OK);
                return TRUE;
            }
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
// 验证当前用户密码是否正确
BOOL VerifyCurrentUserPassword(HWND hParent, const char* password) {
    char userName[256];
    DWORD size = sizeof(userName);
    if (!GetUserName(userName, &size)) return FALSE;

    HANDLE hToken;
    BOOL result = LogonUser(userName, ".", password, LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &hToken);
    if (result) CloseHandle(hToken);
    return result;
}

// 弹出密码验证对话框，返回 TRUE 表示验证通过
BOOL ShowPasswordVerifyDialog(HWND hParent) {
    char userName[256];
    DWORD size = sizeof(userName);
    GetUserName(userName, &size);

    INT_PTR ret = DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_PASSWORD_VERIFY), hParent, PasswordVerifyDlgProc, (LPARAM)userName);
    return (ret == IDOK);
}

// 密码验证对话框过程
INT_PTR CALLBACK PasswordVerifyDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static char* pUserName = NULL;
    switch (msg) {
    case WM_INITDIALOG:
        pUserName = (char*)lParam;
        SetDlgItemText(hDlg, IDC_STATIC_USERNAME, pUserName);
        // 居中对话框
        {
            RECT rcDlg;
            GetWindowRect(hDlg, &rcDlg);
            int cxScreen = GetSystemMetrics(SM_CXSCREEN);
            int cyScreen = GetSystemMetrics(SM_CYSCREEN);
            int x = (cxScreen - (rcDlg.right - rcDlg.left)) / 2;
            int y = (cyScreen - (rcDlg.bottom - rcDlg.top)) / 2;
            SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            char password[256];
            GetDlgItemText(hDlg, IDC_EDIT_VERIFY_PASS, password, sizeof(password));
            if (VerifyCurrentUserPassword(hDlg, password)) {
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBox(hDlg, "密码错误，请重试。", "验证失败", MB_ICONERROR);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK HelpDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_HELP_EDIT, HELP_TEXT);
        return TRUE;
    case WM_CLOSE:
        ShowWindow(hDlg, SW_HIDE);   // 隐藏而不是销毁
        return TRUE;
    }
    return FALSE;
}

// ====================== 应用重启 ======================
static void RestartApplication(void) {
    // 1. 停止网关（关闭 OPC 连接、Modbus 监听等）
    StopGateway();

    // 2. 释放互斥体，使新进程可以正常启动
    if (g_hMutex) {
        CloseHandle(g_hMutex);
        g_hMutex = NULL;
    }

    // 3. 获取当前程序路径
    char exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    // 4. 启动新进程（使用当前目录和命令行）
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcess(NULL, exePath, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // 5. 退出当前进程
    ExitProcess(0);
}

// ====================== 主窗口过程 ======================
INT_PTR CALLBACK MainWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static NOTIFYICONDATA nid = { 0 };
    switch (msg) {
    case WM_INITDIALOG:
        g_hMainWnd = hDlg;
        {
            HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN_ICON));
            SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            HWND hList = GetDlgItem(hDlg, IDC_LIST_DATA);
            LVCOLUMN lvc = { 0 }; lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; lvc.fmt = LVCFMT_LEFT;
            const char* titles[] = { "服务器","OPC Item ID","类型","OPC 值","OPC HEX","Modbus 地址","字节序","Modbus HEX","VT" };
            const int widths[] = { 80, 200, 60, 80, 120, 80, 80, 120, 40 };
            for (int i = 0; i < 9; i++) { lvc.pszText = (char*)titles[i]; lvc.cx = widths[i]; ListView_InsertColumn(hList, i, &lvc); }
            ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        }
        {
            char status[128];
            sprintf(status, "ModbusTCP服务器 端口: %d  从站 ID: %d         OPC服务器: %d  条目: %d", g_cfg.tcp_port, g_cfg.slave_id, g_serverCount, g_mappingCount);
            SetDlgItemText(hDlg, IDC_STATUS_BAR, status);
        }
        UpdateServerStatus(GetDlgItem(hDlg, IDC_SERVER_STATUS));
        nid.cbSize = sizeof(NOTIFYICONDATA); nid.hWnd = hDlg; nid.uID = 1; nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAY_ICON;
        nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_TRAY_ICON));
        strcpy(nid.szTip, "OPC -> Modbus 网关"); Shell_NotifyIcon(NIM_ADD, &nid);
        // 居中窗体
        RECT rcDlg;
        GetWindowRect(hDlg, &rcDlg);
        int cxScreen = GetSystemMetrics(SM_CXSCREEN);
        int cyScreen = GetSystemMetrics(SM_CYSCREEN);
        int x = (cxScreen - (rcDlg.right - rcDlg.left)) / 2;
        int y = (cyScreen - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return TRUE;
    case WM_CLOSE: ShowWindow(hDlg, SW_HIDE); return TRUE;
    case WM_TRAY_ICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt); HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 1, "显示窗口"); AppendMenu(hMenu, MF_STRING, 2, "退出");
            SetForegroundWindow(hDlg); TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL); DestroyMenu(hMenu);
        }
        else if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hDlg, SW_SHOW); SetForegroundWindow(hDlg); }
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: ShowWindow(hDlg, SW_SHOW); SetForegroundWindow(hDlg); break;
        case 2:   // 退出
            if (ShowPasswordVerifyDialog(hDlg)) {
                Shell_NotifyIcon(NIM_DELETE, &nid);
                DestroyWindow(hDlg);
            }
            break;
        case IDC_BTN_SETTINGS: {
            INT_PTR ret = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CONFIG_DIALOG), hDlg, ConfigDlgProc);
            if (ret == IDOK) {
                // 用户点击了“保存并启动”，配置已保存，提示并重启
                int msgRet = MessageBox(hDlg, "配置已保存，程序将重启生效。是否立即重启？", "提示", MB_OKCANCEL | MB_ICONINFORMATION);
                if (msgRet == IDOK) {
                    RestartApplication();  // 重启程序
                }
            }
            // 如果是 IDCANCEL，直接返回，不提示不重启
            break;
        }
        case IDC_BTN_RELOAD:
            if (ShowPasswordVerifyDialog(hDlg)) {
                // 暂停 OPC 读取线程
                g_running = 0;
                if (g_hOPCThread) {
                    WaitForSingleObject(g_hOPCThread, INFINITE);
                    CloseHandle(g_hOPCThread);
                    g_hOPCThread = NULL;
                }
                // 断开现有 OPC 连接
                for (int i = 0; i < g_serverCount; i++) {
                    OPC_Disconnect(&g_servers[i]);
                }
                // 重新加载 CSV 并建立连接
                LoadServers("servers.csv");
                if (LoadMapping(g_cfg.mapping_path) == 0) {
                    for (int i = 0; i < g_serverCount; i++) {
                        if (OPC_Connect(&g_servers[i]) == 0) {
                            OPC_SetupGroupAndItems(&g_servers[i]);
                        }
                    }
                    // 更新状态栏和表格
                    char status[128];
                    sprintf(status, "ModbusTCP服务器 端口 : % d  从站 ID : % d         OPC服务器 : % d  条目 : % d",
                        g_cfg.tcp_port, g_cfg.slave_id, g_serverCount, g_mappingCount);
                    SetDlgItemText(hDlg, IDC_STATUS_BAR, status);
                    UpdateServerStatus(GetDlgItem(hDlg, IDC_SERVER_STATUS));
                    UpdateListView(GetDlgItem(hDlg, IDC_LIST_DATA));
                }
                // 重启 OPC 读取线程
                g_running = 1;
                g_hOPCThread = (HANDLE)_beginthreadex(NULL, 0, OPCThread, NULL, 0, NULL);
            }
        case IDC_BTN_HELP:
            if (!g_hHelpDlg) {
                g_hHelpDlg = CreateDialog(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_HELP_DIALOG), hDlg, HelpDlgProc);
            }
            if (g_hHelpDlg) {
                ShowWindow(g_hHelpDlg, SW_SHOW);
                SetForegroundWindow(g_hHelpDlg);
            }
            break;
        }

        return TRUE;
    case WM_UPDATE_TABLE:
        UpdateListViewData(GetDlgItem(hDlg, IDC_LIST_DATA));   // 仅更新数据，保持滚动位置
        UpdateServerStatus(GetDlgItem(hDlg, IDC_SERVER_STATUS));
        return TRUE;
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        if (g_hHelpDlg) {
            DestroyWindow(g_hHelpDlg);
            g_hHelpDlg = NULL;
        }
        return TRUE;
    }
    return FALSE;
}