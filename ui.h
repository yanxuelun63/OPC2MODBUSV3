#ifndef UI_H
#define UI_H

#include <windows.h>
#include <commctrl.h>
//#include "opc_ids.h"

INT_PTR CALLBACK ConfigDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MainWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateServerStatus(HWND hEdit);
void UpdateListView(HWND hList);

extern HWND g_hMainWnd;
extern BOOL g_bExporting;
extern HANDLE g_hExportThread;

#endif