#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <ole2.h>
#include <objbase.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include <wincrypt.h>

// 官方 GUID
static const CLSID CLSID_OpcServerList = { 0x13486D51,0x4821,0x11D2,{0xA4,0x94,0x3C,0xB3,0x06,0xC1,0x00,0x00} };
static const IID   IID_IOPCServerList = { 0x13486D50,0x4821,0x11D2,{0xA4,0x94,0x3C,0xB3,0x06,0xC1,0x00,0x00} };
static const GUID  CATID_OPCDAServer20 = { 0x63D5F432,0xCFE4,0x11d1,{0xB2,0xC8,0x00,0x60,0x08,0x3B,0xA1,0xFB} };
static const GUID  CATID_OPCDAServer30 = { 0xCC603642,0x66D7,0x48f1,{0xB6,0x9A,0xB6,0x25,0xE7,0x36,0x52,0xD7} };
static const GUID  CATID_XMLDAServer10 = { 0x3098EDA4,0xA006,0x48b2,{0xA2,0x7F,0x24,0x74,0x53,0x95,0x94,0x08} };

typedef GUID CATID;

typedef struct IOPCServerList IOPCServerList;
typedef struct IOPCEnumGUID IOPCEnumGUID;

typedef struct IOPCServerListVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(IOPCServerList* This, REFIID riid, void** ppv);
    ULONG(STDMETHODCALLTYPE* AddRef)(IOPCServerList* This);
    ULONG(STDMETHODCALLTYPE* Release)(IOPCServerList* This);
    HRESULT(STDMETHODCALLTYPE* EnumClassesOfCategories)(
        IOPCServerList* This, ULONG cImplemented, CATID* rgcatidImpl,
        ULONG cRequired, CATID* rgcatidReq, IOPCEnumGUID** ppenum);
    HRESULT(STDMETHODCALLTYPE* GetClassDetails)(
        IOPCServerList* This, REFCLSID rclsid, LPOLESTR* ppszProgID, LPOLESTR* ppszUserType);
    HRESULT(STDMETHODCALLTYPE* CLSIDFromProgID)(
        IOPCServerList* This, LPCOLESTR pszProgID, CLSID* pclsid);
} IOPCServerListVtbl;
struct IOPCServerList { IOPCServerListVtbl* lpVtbl; };

typedef struct IOPCEnumGUIDVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(IOPCEnumGUID* This, REFIID riid, void** ppv);
    ULONG(STDMETHODCALLTYPE* AddRef)(IOPCEnumGUID* This);
    ULONG(STDMETHODCALLTYPE* Release)(IOPCEnumGUID* This);
    HRESULT(STDMETHODCALLTYPE* Next)(IOPCEnumGUID* This, ULONG celt, GUID* rgelt, ULONG* pceltFetched);
    HRESULT(STDMETHODCALLTYPE* Skip)(IOPCEnumGUID* This, ULONG celt);
    HRESULT(STDMETHODCALLTYPE* Reset)(IOPCEnumGUID* This);
    HRESULT(STDMETHODCALLTYPE* Clone)(IOPCEnumGUID* This, IOPCEnumGUID** ppenum);
} IOPCEnumGUIDVtbl;
struct IOPCEnumGUID { IOPCEnumGUIDVtbl* lpVtbl; };

// 去重集合
#define MAX_DUPES 512
static WCHAR* gDupes[MAX_DUPES];
static int gDupesCount = 0;

static void ClearDupes(void) {
    for (int i = 0; i < gDupesCount; i++) free(gDupes[i]);
    gDupesCount = 0;
}

static BOOL IsDuplicate(const WCHAR* progID) {
    for (int i = 0; i < gDupesCount; i++)
        if (wcscmp(gDupes[i], progID) == 0) return TRUE;
    if (gDupesCount < MAX_DUPES) {
        gDupes[gDupesCount] = _wcsdup(progID);
        gDupesCount++;
    }
    return FALSE;
}

static void AddServerToList(HWND hList, const WCHAR* progID, const WCHAR* clsidStr) {
    if (IsDuplicate(progID)) return;
    char szProgID[256], szClsid[256];
    WideCharToMultiByte(CP_ACP, 0, progID, -1, szProgID, sizeof(szProgID), NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, clsidStr, -1, szClsid, sizeof(szClsid), NULL, NULL);
    LVITEM lvi = { 0 };
    lvi.mask = LVIF_TEXT;
    lvi.iItem = 9999;
    lvi.pszText = szProgID;
    int idx = ListView_InsertItem(hList, &lvi);
    ListView_SetItemText(hList, idx, 1, szClsid);
}

static int EnumCategoryCOM(IOPCServerList* pSvr, const GUID* cat, HWND hList) {
    IOPCEnumGUID* pEnum = NULL;
    HRESULT hr = pSvr->lpVtbl->EnumClassesOfCategories(pSvr, 1, (CATID*)cat, 0, NULL, &pEnum);
    if (FAILED(hr)) return 0;

    int count = 0;
    CLSID cls; ULONG fetched;
    while (pEnum->lpVtbl->Next(pEnum, 1, &cls, &fetched) == S_OK && fetched == 1) {
        LPOLESTR progID = NULL, userType = NULL;
        if (SUCCEEDED(pSvr->lpVtbl->GetClassDetails(pSvr, &cls, &progID, &userType))) {
            WCHAR clsidStr[256];
            if (StringFromGUID2(&cls, clsidStr, 256) == 0) {
                wcscpy(clsidStr, L"{????????-????-????-????-????????????}");
            }
            AddServerToList(hList, progID, clsidStr);
            CoTaskMemFree(progID);
            if (userType) CoTaskMemFree(userType);
            count++;
        }
        else {
            LPOLESTR pProgID = NULL;
            if (SUCCEEDED(ProgIDFromCLSID(&cls, &pProgID))) {
                WCHAR clsidStr[256];
                if (StringFromGUID2(&cls, clsidStr, 256) == 0) {
                    wcscpy(clsidStr, L"{????????-????-????-????-????????????}");
                }
                AddServerToList(hList, pProgID, clsidStr);
                CoTaskMemFree(pProgID);
                count++;
            }
        }
    }
    pEnum->lpVtbl->Release(pEnum);
    return count;
}

static int EnumCategoryRegistryLocal(HWND hList, const GUID* cat) {
    WCHAR catStr[256];
    if (StringFromGUID2(cat, catStr, 256) == 0) {
        wcscpy(catStr, L"{????????-????-????-????-????????????}");
    }
    HKEY hCLSID;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &hCLSID) != ERROR_SUCCESS) return 0;
    int count = 0;
    DWORD index = 0;
    WCHAR szCLSID[256];
    while (RegEnumKeyW(hCLSID, index, szCLSID, 256) == ERROR_SUCCESS) {
        WCHAR catPath[512];
        swprintf_s(catPath, 512, L"CLSID\\%s\\Implemented Categories\\%s", szCLSID, catStr);
        HKEY hCat;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, catPath, 0, KEY_READ, &hCat) == ERROR_SUCCESS) {
            RegCloseKey(hCat);
            WCHAR progIDPath[512];
            swprintf_s(progIDPath, 512, L"CLSID\\%s\\ProgID", szCLSID);
            HKEY hProgID;
            if (RegOpenKeyExW(HKEY_CLASSES_ROOT, progIDPath, 0, KEY_READ, &hProgID) == ERROR_SUCCESS) {
                WCHAR progID[256];
                DWORD size = sizeof(progID);
                if (RegQueryValueExW(hProgID, NULL, NULL, NULL, (LPBYTE)progID, &size) == ERROR_SUCCESS) {
                    AddServerToList(hList, progID, szCLSID);
                    count++;
                }
                RegCloseKey(hProgID);
            }
        }
        index++;
    }
    RegCloseKey(hCLSID);
    return count;
}

int EnumRemoteOPCServers(HWND hList, const char* ip, const char* user, const char* pass) {
    ListView_DeleteAllItems(hList);
    ClearDupes();

    BOOL isRemote = (ip && ip[0] && strcmp(ip, "127.0.0.1") != 0);
    int totalCount = 0;

    if (isRemote) {
        // ---- 远程 COM 枚举（使用进程已初始化的 COM，不再重复初始化） ----
        IOPCServerList* pSvr = NULL;
        MULTI_QI mqi = { &IID_IOPCServerList, NULL, S_OK };
        HRESULT hr;
        BOOL bSuccess = FALSE;

        // 准备 IP 宽字符串（两阶段共用）
        wchar_t ipWide[64];
        MultiByteToWideChar(CP_ACP, 0, ip, -1, ipWide, 64);

        // ---------- 尝试1：匿名（NONE 级别）----------
        {
            COAUTHINFO authInfo = { 0 };
            authInfo.dwAuthnSvc = RPC_C_AUTHN_WINNT;
            authInfo.dwAuthzSvc = RPC_C_AUTHZ_NONE;
            authInfo.dwAuthnLevel = RPC_C_AUTHN_LEVEL_NONE;
            authInfo.dwImpersonationLevel = RPC_C_IMP_LEVEL_IMPERSONATE;
            authInfo.pAuthIdentityData = NULL;
            authInfo.dwCapabilities = EOAC_NONE;

            COSERVERINFO csi = { 0 };
            csi.pwszName = ipWide;
            csi.pAuthInfo = &authInfo;

            hr = CoCreateInstanceEx(&CLSID_OpcServerList, NULL, CLSCTX_REMOTE_SERVER, &csi, 1, &mqi);
            if (SUCCEEDED(hr) && SUCCEEDED(mqi.hr)) {
                bSuccess = TRUE;
            }
        }

        // ---------- 尝试2：如果失败且提供了用户名密码，使用凭据（CONNECT 级别）----------
        if (!bSuccess && user && user[0] && pass && pass[0]) {
            // 解密密码（如果以 ENC: 开头，此处临时用明文，正式版请调用 DecryptPassword）
            char password[256] = { 0 };
            if (strncmp(pass, "ENC:", 4) == 0) {
                if (!DecryptPassword(pass + 4, password, sizeof(password))) {
                    // 解密失败，直接返回错误
                    MessageBox(hList, "密码解密失败，无法进行远程枚举。", "错误", MB_ICONERROR);
                    return -1;
                }
            }
            else {
                strncpy(password, pass, sizeof(password) - 1);
            }

            wchar_t wUser[64] = { 0 };
            wchar_t wPass[256] = { 0 };
            MultiByteToWideChar(CP_ACP, 0, user, -1, wUser, 64);
            MultiByteToWideChar(CP_ACP, 0, password, -1, wPass, 256);

            COAUTHIDENTITY authIdent = { 0 };
            authIdent.User = wUser;
            authIdent.Password = wPass;
            authIdent.Domain = L"";
            authIdent.UserLength = (DWORD)wcslen(wUser);
            authIdent.PasswordLength = (DWORD)wcslen(wPass);
            authIdent.Flags = SEC_WINNT_AUTH_IDENTITY_UNICODE;

            COAUTHINFO authInfo2 = { 0 };
            authInfo2.dwAuthnSvc = RPC_C_AUTHN_WINNT;
            authInfo2.dwAuthzSvc = RPC_C_AUTHZ_NONE;
            authInfo2.dwAuthnLevel = RPC_C_AUTHN_LEVEL_CONNECT;
            authInfo2.dwImpersonationLevel = RPC_C_IMP_LEVEL_IMPERSONATE;
            authInfo2.pAuthIdentityData = &authIdent;
            authInfo2.dwCapabilities = EOAC_NONE;

            COSERVERINFO csi2 = { 0 };
            csi2.pwszName = ipWide;
            csi2.pAuthInfo = &authInfo2;

            hr = CoCreateInstanceEx(&CLSID_OpcServerList, NULL, CLSCTX_REMOTE_SERVER, &csi2, 1, &mqi);
            if (SUCCEEDED(hr) && SUCCEEDED(mqi.hr)) {
                bSuccess = TRUE;
            }
        }

        // 处理结果
        if (bSuccess) {
            pSvr = (IOPCServerList*)mqi.pItf;
            const GUID* cats[] = { &CATID_OPCDAServer20, &CATID_OPCDAServer30, &CATID_XMLDAServer10 };
            for (int i = 0; i < 3; i++) totalCount += EnumCategoryCOM(pSvr, cats[i], hList);
            if (pSvr)
            pSvr->lpVtbl->Release(pSvr);
        }
        else {
            char msg[256];
            sprintf(msg, "远程枚举失败\nhr=0x%08X, mqi.hr=0x%08X", hr, mqi.hr);
            MessageBox(hList, msg, "枚举错误", MB_ICONERROR);
        }
    }
    else {
        // ---- 本地枚举（保持不变） ----
        IOPCServerList* pSvr = NULL;
        HRESULT hr = CoCreateInstance(&CLSID_OpcServerList, NULL,
            CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
            &IID_IOPCServerList, (void**)&pSvr);
        if (SUCCEEDED(hr) && pSvr != NULL) {
            const GUID* cats[] = { &CATID_OPCDAServer20, &CATID_OPCDAServer30, &CATID_XMLDAServer10 };
            for (int i = 0; i < 3; i++) totalCount += EnumCategoryCOM(pSvr, cats[i], hList);
            pSvr->lpVtbl->Release(pSvr);
        }
        else {
            const GUID* cats[] = { &CATID_OPCDAServer20, &CATID_OPCDAServer30, &CATID_XMLDAServer10 };
            for (int i = 0; i < 3; i++) totalCount += EnumCategoryRegistryLocal(hList, cats[i]);
        }
        if (totalCount == 0) {
            MessageBox(hList, "未发现 OPC 服务器。", "提示", MB_OK);
        }
    }

    return totalCount;
}