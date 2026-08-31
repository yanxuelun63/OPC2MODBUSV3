#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <ole2.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "opc_ids.h"
#include "opc_browse.h"
#include "servers.h"
#include "logger.h"

typedef DWORD OPCHANDLE;

typedef enum tagOPCNAMESPACETYPE { OPC_NS_HIERARCHIAL = 1, OPC_NS_FLAT = 2 } OPCNAMESPACETYPE;
typedef enum tagOPCBROWSEDIRECTION { OPC_BROWSE_UP = 1, OPC_BROWSE_DOWN = 2, OPC_BROWSE_TO = 3 } OPCBROWSEDIRECTION;
typedef enum tagOPCBROWSETYPE { OPC_BRANCH = 1, OPC_LEAF = 2, OPC_FLAT = 3 } OPCBROWSETYPE;

static const IID IID_IOPCBrowseServerAddressSpace = { 0x39c13a4f,0x011e,0x11d0,{0x96,0x75,0x00,0x20,0xaf,0xd8,0xad,0xb3} };
static const IID IID_IOPCItemProperties = { 0x39c13a72,0x011e,0x11d0,{0x96,0x75,0x00,0x20,0xaf,0xd8,0xad,0xb3} };

typedef struct IOPCServer IOPCServer;
typedef struct IOPCServerVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(IOPCServer* This, REFIID riid, void** ppv);
    ULONG(STDMETHODCALLTYPE* AddRef)(IOPCServer* This);
    ULONG(STDMETHODCALLTYPE* Release)(IOPCServer* This);
    HRESULT(STDMETHODCALLTYPE* AddGroup)(IOPCServer* This, LPCWSTR szName, BOOL bActive, DWORD dwRequestedUpdateRate, OPCHANDLE hClientGroup, LONG* pTimeBias, FLOAT* pPercentDeadband, DWORD dwLCID, OPCHANDLE* phServerGroup, DWORD* pRevisedUpdateRate, REFIID riid, void** ppUnk);
    HRESULT(STDMETHODCALLTYPE* GetErrorString)(IOPCServer* This, HRESULT dwError, LCID dwLocale, WCHAR** ppString);
    HRESULT(STDMETHODCALLTYPE* GetGroupByName)(IOPCServer* This, LPCWSTR szName, REFIID riid, void** ppUnk);
    HRESULT(STDMETHODCALLTYPE* GetStatus)(IOPCServer* This, void** ppServerStatus);
    HRESULT(STDMETHODCALLTYPE* RemoveGroup)(IOPCServer* This, OPCHANDLE hServerGroup, BOOL bForce);
    HRESULT(STDMETHODCALLTYPE* CreateGroupEnumerator)(IOPCServer* This, DWORD dwScope, REFIID riid, void** ppUnk);
} IOPCServerVtbl;
struct IOPCServer { IOPCServerVtbl* lpVtbl; };

typedef struct IOPCBrowseServerAddressSpace IOPCBrowseServerAddressSpace;
typedef struct IOPCBrowseServerAddressSpaceVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(IOPCBrowseServerAddressSpace* This, REFIID riid, void** ppv);
    ULONG(STDMETHODCALLTYPE* AddRef)(IOPCBrowseServerAddressSpace* This);
    ULONG(STDMETHODCALLTYPE* Release)(IOPCBrowseServerAddressSpace* This);
    HRESULT(STDMETHODCALLTYPE* QueryOrganization)(IOPCBrowseServerAddressSpace* This, OPCNAMESPACETYPE* pNameSpaceType);
    HRESULT(STDMETHODCALLTYPE* ChangeBrowsePosition)(IOPCBrowseServerAddressSpace* This, OPCBROWSEDIRECTION dwBrowseDirection, LPCWSTR szString);
    HRESULT(STDMETHODCALLTYPE* BrowseOPCItemIDs)(IOPCBrowseServerAddressSpace* This, OPCBROWSETYPE dwBrowseFilterType, LPCWSTR szFilterCriteria, VARTYPE vtDataTypeFilter, DWORD dwAccessRightsFilter, LPENUMSTRING* ppIEnumString);
    HRESULT(STDMETHODCALLTYPE* GetItemID)(IOPCBrowseServerAddressSpace* This, LPWSTR szItemDataID, LPWSTR* szItemID);
    HRESULT(STDMETHODCALLTYPE* BrowseAccessPaths)(IOPCBrowseServerAddressSpace* This, LPCWSTR szItemID, LPENUMSTRING* ppIEnumString);
} IOPCBrowseServerAddressSpaceVtbl;
struct IOPCBrowseServerAddressSpace { IOPCBrowseServerAddressSpaceVtbl* lpVtbl; };

typedef struct IOPCItemProperties IOPCItemProperties;
typedef struct IOPCItemPropertiesVtbl {
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(IOPCItemProperties* This, REFIID riid, void** ppv);
    ULONG(STDMETHODCALLTYPE* AddRef)(IOPCItemProperties* This);
    ULONG(STDMETHODCALLTYPE* Release)(IOPCItemProperties* This);
    HRESULT(STDMETHODCALLTYPE* QueryAvailableProperties)(IOPCItemProperties* This, LPWSTR szItemID, DWORD* pdwCount, DWORD** ppPropertyIDs, LPWSTR** ppDescriptions, VARTYPE** ppvtDataTypes);
    HRESULT(STDMETHODCALLTYPE* GetItemProperties)(IOPCItemProperties* This, LPWSTR szItemID, DWORD dwCount, DWORD* pdwPropertyIDs, VARIANT** ppvData, HRESULT** ppErrors);
    HRESULT(STDMETHODCALLTYPE* LookupItemIDs)(IOPCItemProperties* This, LPWSTR szItemID, DWORD dwCount, DWORD* pdwPropertyIDs, LPWSTR** ppszNewItemIDs, HRESULT** ppErrors);
} IOPCItemPropertiesVtbl;
struct IOPCItemProperties { IOPCItemPropertiesVtbl* lpVtbl; };

#define OPC_PROPERTY_DATATYPE 1
#define MAX_ITEMS_PER_LEVEL 20000

// 全局路径分隔符，默认为点，可动态调整为斜杠
static WCHAR g_pathSeparator[2] = L".";
static int s_exportCount = 0;

static const char* VariantTypeToString(VARTYPE vt) {
    switch (vt) {
    case VT_BOOL: return "Bool";
    case VT_I1:   return "SByte";
    case VT_UI1:  return "Byte";
    case VT_I2:   return "Int";
    case VT_UI2:  return "Word";
    case VT_I4:   return "Long";
    case VT_UI4:  return "DWord";
    case VT_R4:   return "Float";
    case VT_R8:   return "Double";
    case VT_BSTR: return "String";
    default:      return "Unknown";
    }
}

static VARTYPE GetItemDataType(IOPCItemProperties* pProps, LPCWSTR szItemID) {
    if (!pProps) return VT_EMPTY;
    DWORD propID = OPC_PROPERTY_DATATYPE;
    VARIANT* pVar = NULL;
    HRESULT* pErrors = NULL;
    HRESULT hr = pProps->lpVtbl->GetItemProperties(pProps, (LPWSTR)szItemID, 1, &propID, &pVar, &pErrors);
    VARTYPE vt = VT_EMPTY;
    if (SUCCEEDED(hr) && pVar && SUCCEEDED(pErrors[0]) && pVar[0].vt == VT_I2)
        vt = (VARTYPE)pVar[0].iVal;
    if (pVar) { VariantClear(pVar); CoTaskMemFree(pVar); }
    if (pErrors) CoTaskMemFree(pErrors);
    return vt;
}

static void GoToRoot(IOPCBrowseServerAddressSpace* pBrowser) {
    HRESULT hr;
    do {
        hr = pBrowser->lpVtbl->ChangeBrowsePosition(pBrowser, OPC_BROWSE_UP, NULL);
    } while (SUCCEEDED(hr));
}

// 只输出纯叶子，分支不输出。对子分支使用绝对路径跳转，确保每个分支独立进入。
static void BrowseRelativeLeaves(IOPCBrowseServerAddressSpace* pBrowser, IOPCItemProperties* pProps,
    const WCHAR* currentPath, FILE* fp, const char* serverName, HWND hProgressWnd) {
    LPENUMSTRING pEnum = NULL;
    HRESULT hr;

    WCHAR(*leafNames)[256] = (WCHAR(*)[256])malloc(MAX_ITEMS_PER_LEVEL * sizeof(WCHAR[256]));
    WCHAR(*branchNames)[256] = (WCHAR(*)[256])malloc(MAX_ITEMS_PER_LEVEL * sizeof(WCHAR[256]));
    if (!leafNames || !branchNames) {
        free(leafNames);
        free(branchNames);
        return;
    }
    int leafCount = 0, branchCount = 0;

    // 收集叶子
    hr = pBrowser->lpVtbl->BrowseOPCItemIDs(pBrowser, OPC_LEAF, L"", VT_EMPTY, 0, &pEnum);
    if (SUCCEEDED(hr) && pEnum) {
        LPOLESTR pID; ULONG fetched;
        while (pEnum->lpVtbl->Next(pEnum, 1, &pID, &fetched) == S_OK && fetched == 1 && leafCount < MAX_ITEMS_PER_LEVEL) {
            wcscpy(leafNames[leafCount], pID);
            CoTaskMemFree(pID);
            leafCount++;
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    // 收集分支
    hr = pBrowser->lpVtbl->BrowseOPCItemIDs(pBrowser, OPC_BRANCH, L"", VT_EMPTY, 0, &pEnum);
    if (SUCCEEDED(hr) && pEnum) {
        LPOLESTR pID; ULONG fetched;
        while (pEnum->lpVtbl->Next(pEnum, 1, &pID, &fetched) == S_OK && fetched == 1 && branchCount < MAX_ITEMS_PER_LEVEL) {
            wcscpy(branchNames[branchCount], pID);
            CoTaskMemFree(pID);
            branchCount++;
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    // 输出纯叶子（不在分支列表中的叶子），优先使用GetItemID获取权威路径
    for (int i = 0; i < leafCount; i++) {
        BOOL isBranch = FALSE;
        for (int j = 0; j < branchCount; j++) {
            if (wcscmp(leafNames[i], branchNames[j]) == 0) {
                isBranch = TRUE;
                break;
            }
        }
        if (!isBranch) {
            // 优先使用GetItemID获取服务器认可的完整路径
            LPWSTR fullPath = NULL;
            if (SUCCEEDED(pBrowser->lpVtbl->GetItemID(pBrowser, leafNames[i], &fullPath)) && fullPath) {
                // 获取成功，使用官方路径
                VARTYPE vt = GetItemDataType(pProps, fullPath);
                const char* typeStr = VariantTypeToString(vt);
                char szID[512];
                WideCharToMultiByte(CP_ACP, 0, fullPath, -1, szID, sizeof(szID), NULL, NULL);
                fprintf(fp, "%s,%s,%s\n", serverName, szID, typeStr);
                CoTaskMemFree(fullPath);
            }
            else {
                // 回退到手动拼接路径（兼容少数服务器）
                WCHAR manualPath[512];
                if (currentPath[0])
                    swprintf(manualPath, 512, L"%s%s%s", currentPath, g_pathSeparator, leafNames[i]);
                else
                    wcscpy(manualPath, leafNames[i]);

                VARTYPE vt = GetItemDataType(pProps, manualPath);
                const char* typeStr = VariantTypeToString(vt);
                char szID[512];
                WideCharToMultiByte(CP_ACP, 0, manualPath, -1, szID, sizeof(szID), NULL, NULL);
                fprintf(fp, "%s,%s,%s\n", serverName, szID, typeStr);
            }
            s_exportCount++;
            if (hProgressWnd && (s_exportCount % 10 == 0)) {
                PostMessage(hProgressWnd, WM_EXPORT_PROGRESS, (WPARAM)s_exportCount, 0);
            }
        }
    }

    // 对所有分支（包括伪叶子）使用绝对路径跳转进入
    for (int i = 0; i < branchCount; i++) {
        WCHAR childAbsolutePath[512];
        if (currentPath[0])
            swprintf(childAbsolutePath, 512, L"%s%s%s", currentPath, g_pathSeparator, branchNames[i]);
        else
            wcscpy(childAbsolutePath, branchNames[i]);

        GoToRoot(pBrowser);
        hr = pBrowser->lpVtbl->ChangeBrowsePosition(pBrowser, OPC_BROWSE_TO, childAbsolutePath);
        if (SUCCEEDED(hr)) {
            BrowseRelativeLeaves(pBrowser, pProps, childAbsolutePath, fp, serverName, hProgressWnd);
        }
    }

    // 每层递归结束时发送一次进度，确保累积值可见
    if (hProgressWnd) {
        PostMessage(hProgressWnd, WM_EXPORT_PROGRESS, (WPARAM)s_exportCount, 0);
    }

    free(leafNames);
    free(branchNames);
}

int BrowseAndExportItems(OPCServerInfo* srv, HWND hProgressWnd) {
    s_exportCount = 0;
    if (!srv || !srv->pOPCServer) return -1;

    IOPCBrowseServerAddressSpace* pBrowser = NULL;
    HRESULT hr = srv->pOPCServer->lpVtbl->QueryInterface(srv->pOPCServer, &IID_IOPCBrowseServerAddressSpace, (void**)&pBrowser);
    if (FAILED(hr)) {
        Logger_Write("服务器 %s 不支持浏览地址空间接口", srv->name);
        return -1;
    }

    IOPCItemProperties* pProps = NULL;
    srv->pOPCServer->lpVtbl->QueryInterface(srv->pOPCServer, &IID_IOPCItemProperties, (void**)&pProps);
    if (!pProps) Logger_Write("服务器 %s 不支持 IOPCItemProperties 接口，数据类型将显示为 Unknown", srv->name);

    // ---------- 自动检测路径分隔符 ----------
    GoToRoot(pBrowser);
    LPWSTR pSampleBranch = NULL;
    LPENUMSTRING pEnumBranch = NULL;
    BOOL found = FALSE;
    HRESULT hrBranch = pBrowser->lpVtbl->BrowseOPCItemIDs(pBrowser, OPC_BRANCH, L"", VT_EMPTY, 0, &pEnumBranch);
    if (SUCCEEDED(hrBranch) && pEnumBranch) {
        LPOLESTR pFirstBranch = NULL;
        ULONG fetched;
        if (pEnumBranch->lpVtbl->Next(pEnumBranch, 1, &pFirstBranch, &fetched) == S_OK && fetched == 1) {
            pSampleBranch = _wcsdup(pFirstBranch);
            CoTaskMemFree(pFirstBranch);
        }
        pEnumBranch->lpVtbl->Release(pEnumBranch);
    }

    if (pSampleBranch) {
        LPENUMSTRING pEnumFlat = NULL;
        HRESULT hrFlat = pBrowser->lpVtbl->BrowseOPCItemIDs(pBrowser, OPC_FLAT, L"", VT_EMPTY, 0, &pEnumFlat);
        if (SUCCEEDED(hrFlat) && pEnumFlat) {
            LPOLESTR pItem;
            ULONG fetched;
            while (pEnumFlat->lpVtbl->Next(pEnumFlat, 1, &pItem, &fetched) == S_OK && fetched == 1) {
                WCHAR* pos = wcsstr(pItem, pSampleBranch);
                if (pos) {
                    if (pos > pItem) {
                        g_pathSeparator[0] = *(pos - 1);
                        found = TRUE;
                    }
                    else if (pos[wcslen(pSampleBranch)] != L'\0') {
                        g_pathSeparator[0] = pos[wcslen(pSampleBranch)];
                        found = TRUE;
                    }
                    if (found) break;
                }
                CoTaskMemFree(pItem);
            }
            pEnumFlat->lpVtbl->Release(pEnumFlat);
            if (found) {
                Logger_Write("服务器 %s 检测到路径分隔符: '%c'", srv->name, (char)g_pathSeparator[0]);
            }
        }

        if (!found) {
            LPWSTR pFullID = NULL;
            if (SUCCEEDED(pBrowser->lpVtbl->GetItemID(pBrowser, pSampleBranch, &pFullID)) && pFullID) {
                WCHAR* pos = wcsstr(pFullID, pSampleBranch);
                if (pos) {
                    if (pos > pFullID && *(pos - 1) != L'.') {
                        g_pathSeparator[0] = *(pos - 1);
                    }
                    else if (pos[wcslen(pSampleBranch)] != L'\0' && pos[wcslen(pSampleBranch)] != L'.') {
                        g_pathSeparator[0] = pos[wcslen(pSampleBranch)];
                    }
                }
                CoTaskMemFree(pFullID);
            }
        }
        free(pSampleBranch);
    }

    if (!found) {
        Logger_Write("服务器 %s 未检测到分隔符，使用默认点 '.'", srv->name);
    }
    else {
        Logger_Write("服务器 %s 最终采用分隔符: '%c'", srv->name, (char)g_pathSeparator[0]);
    }

    SYSTEMTIME st; GetLocalTime(&st);
    char filename[MAX_PATH];
    sprintf(filename, "%s_%04d%02d%02d%02d%02d%02d.csv", srv->name, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    FILE* fp = fopen(filename, "w");
    if (!fp) {
        if (pBrowser) pBrowser->lpVtbl->Release(pBrowser);
        if (pProps) pProps->lpVtbl->Release(pProps);
        return -1;
    }
    fprintf(fp, "ServerName,OPCItemID,DataType\n");

    GoToRoot(pBrowser);

    // 1. 输出根下的纯叶子（优先使用GetItemID）
    LPENUMSTRING pEnum = NULL;
    hr = pBrowser->lpVtbl->BrowseOPCItemIDs(pBrowser, OPC_LEAF, L"", VT_EMPTY, 0, &pEnum);
    if (SUCCEEDED(hr) && pEnum) {
        WCHAR(*rootBranches)[256] = (WCHAR(*)[256])malloc(MAX_ITEMS_PER_LEVEL * sizeof(WCHAR[256]));
        int rootBranchCount = 0;
        if (rootBranches) {
            LPENUMSTRING pBranchEnum = NULL;
            HRESULT hrB = pBrowser->lpVtbl->BrowseOPCItemIDs(pBrowser, OPC_BRANCH, L"", VT_EMPTY, 0, &pBranchEnum);
            if (SUCCEEDED(hrB) && pBranchEnum) {
                LPOLESTR pBranch; ULONG fetched;
                while (pBranchEnum->lpVtbl->Next(pBranchEnum, 1, &pBranch, &fetched) == S_OK && fetched == 1 && rootBranchCount < MAX_ITEMS_PER_LEVEL) {
                    wcscpy(rootBranches[rootBranchCount], pBranch);
                    CoTaskMemFree(pBranch);
                    rootBranchCount++;
                }
                pBranchEnum->lpVtbl->Release(pBranchEnum);
            }

            LPOLESTR pItemID; ULONG fetched;
            while (pEnum->lpVtbl->Next(pEnum, 1, &pItemID, &fetched) == S_OK && fetched == 1) {
                BOOL isBranch = FALSE;
                for (int i = 0; i < rootBranchCount; i++) {
                    if (wcscmp(pItemID, rootBranches[i]) == 0) { isBranch = TRUE; break; }
                }
                if (!isBranch) {
                    // 优先使用GetItemID
                    LPWSTR fullID = NULL;
                    if (SUCCEEDED(pBrowser->lpVtbl->GetItemID(pBrowser, pItemID, &fullID)) && fullID) {
                        VARTYPE vt = GetItemDataType(pProps, fullID);
                        const char* typeStr = VariantTypeToString(vt);
                        char szID[512];
                        WideCharToMultiByte(CP_ACP, 0, fullID, -1, szID, sizeof(szID), NULL, NULL);
                        fprintf(fp, "%s,%s,%s\n", srv->name, szID, typeStr);
                        CoTaskMemFree(fullID);
                    }
                    else {
                        // 回退到手动拼接
                        VARTYPE vt = GetItemDataType(pProps, pItemID);
                        const char* typeStr = VariantTypeToString(vt);
                        char szID[512];
                        WideCharToMultiByte(CP_ACP, 0, pItemID, -1, szID, sizeof(szID), NULL, NULL);
                        fprintf(fp, "%s,%s,%s\n", srv->name, szID, typeStr);
                    }
                    s_exportCount++;
                    if (hProgressWnd && (s_exportCount % 10 == 0)) {
                        PostMessage(hProgressWnd, WM_EXPORT_PROGRESS, (WPARAM)s_exportCount, 0);
                    }
                }
                CoTaskMemFree(pItemID);
            }
            free(rootBranches);
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    // 2. 收集根下所有分支名，并用绝对路径跳转进入
    WCHAR(*branches)[256] = (WCHAR(*)[256])malloc(MAX_ITEMS_PER_LEVEL * sizeof(WCHAR[256]));
    int branchCount = 0;
    if (branches) {
        hr = pBrowser->lpVtbl->BrowseOPCItemIDs(pBrowser, OPC_BRANCH, L"", VT_EMPTY, 0, &pEnum);
        if (SUCCEEDED(hr) && pEnum) {
            LPOLESTR pBranch; ULONG fetched;
            while (pEnum->lpVtbl->Next(pEnum, 1, &pBranch, &fetched) == S_OK && fetched == 1 && branchCount < MAX_ITEMS_PER_LEVEL) {
                wcscpy(branches[branchCount], pBranch);
                CoTaskMemFree(pBranch);
                branchCount++;
            }
            pEnum->lpVtbl->Release(pEnum);
        }

        for (int i = 0; i < branchCount; i++) {
            GoToRoot(pBrowser);
            hr = pBrowser->lpVtbl->ChangeBrowsePosition(pBrowser, OPC_BROWSE_TO, branches[i]);
            if (SUCCEEDED(hr)) {
                BrowseRelativeLeaves(pBrowser, pProps, branches[i], fp, srv->name, hProgressWnd);
            }
            else {
                // 进入失败，作为纯叶子输出（并使用GetItemID尝试获取正确路径）
                LPWSTR fullID = NULL;
                if (SUCCEEDED(pBrowser->lpVtbl->GetItemID(pBrowser, branches[i], &fullID)) && fullID) {
                    VARTYPE vt = GetItemDataType(pProps, fullID);
                    const char* typeStr = VariantTypeToString(vt);
                    char szID[512];
                    WideCharToMultiByte(CP_ACP, 0, fullID, -1, szID, sizeof(szID), NULL, NULL);
                    fprintf(fp, "%s,%s,%s\n", srv->name, szID, typeStr);
                    CoTaskMemFree(fullID);
                }
                else {
                    VARTYPE vt = GetItemDataType(pProps, branches[i]);
                    const char* typeStr = VariantTypeToString(vt);
                    char szID[512];
                    WideCharToMultiByte(CP_ACP, 0, branches[i], -1, szID, sizeof(szID), NULL, NULL);
                    fprintf(fp, "%s,%s,%s\n", srv->name, szID, typeStr);
                }
                s_exportCount++;
                if (hProgressWnd && (s_exportCount % 10 == 0)) {
                    PostMessage(hProgressWnd, WM_EXPORT_PROGRESS, (WPARAM)s_exportCount, 0);
                }
            }
        }
        free(branches);
    }

    fclose(fp);

    if (hProgressWnd) {
        PostMessage(hProgressWnd, WM_EXPORT_PROGRESS, (WPARAM)s_exportCount, 0);
        Logger_Write("从服务器 %s 导出条目 %d 条", srv->name, s_exportCount);
    }
    if (pBrowser) pBrowser->lpVtbl->Release(pBrowser);
    if (pProps) pProps->lpVtbl->Release(pProps);
    return s_exportCount;
}