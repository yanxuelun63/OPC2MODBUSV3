#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <wincrypt.h>

AppConfig g_cfg;

void LoadConfig(AppConfig *cfg) {
    cfg->tcp_port   = GetPrivateProfileInt("Modbus", "TCPPort", 502, ".\\config.ini");
    cfg->slave_id   = GetPrivateProfileInt("Modbus", "SlaveID", 1, ".\\config.ini");
    cfg->refresh_ms = GetPrivateProfileInt("OPC", "RefreshInterval", 1000, ".\\config.ini");
    GetPrivateProfileString("Misc", "MappingFile", "mapping.csv",
                            cfg->mapping_path, MAX_PATH, ".\\config.ini");
    // 读取 MappingFile 后，继续读取 AutoStart
    char autoStartStr[16] = "0";
    GetPrivateProfileString("Misc", "AutoStart", "0", autoStartStr, sizeof(autoStartStr), ".\\config.ini");
    cfg->autoStart = (atoi(autoStartStr) != 0) ? 1 : 0;
}

void SaveConfig(const AppConfig *cfg) {
    char buf[16];
    sprintf(buf, "%d", cfg->refresh_ms);
    WritePrivateProfileString("OPC", "RefreshInterval", buf, ".\\config.ini");
    sprintf(buf, "%d", cfg->tcp_port);
    WritePrivateProfileString("Modbus", "TCPPort", buf, ".\\config.ini");
    sprintf(buf, "%d", cfg->slave_id);
    WritePrivateProfileString("Modbus", "SlaveID", buf, ".\\config.ini");
    WritePrivateProfileString("Misc", "MappingFile", cfg->mapping_path, ".\\config.ini");
    // 写入 AutoStart
    char autoStartStr[16];
    sprintf(autoStartStr, "%d", cfg->autoStart);
    WritePrivateProfileString("Misc", "AutoStart", autoStartStr, ".\\config.ini");
}

BOOL DecryptPassword(const char* encBase64, char* outPlain, int outSize) {
    if (!encBase64 || encBase64[0] == '\0') { outPlain[0] = '\0'; return TRUE; }
    DATA_BLOB in, out;
    DWORD binSize = 0;
    CryptStringToBinaryA(encBase64, 0, CRYPT_STRING_BASE64, NULL, &binSize, NULL, NULL);
    if (binSize == 0) return FALSE;
    BYTE* bin = (BYTE*)malloc(binSize);
    if (!bin) return FALSE;
    CryptStringToBinaryA(encBase64, 0, CRYPT_STRING_BASE64, bin, &binSize, NULL, NULL);
    in.pbData = bin; in.cbData = binSize;

    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out)) {
        DWORD len = min(out.cbData, (DWORD)(outSize - 1));
        memcpy(outPlain, out.pbData, len);
        outPlain[len] = '\0';
        LocalFree(out.pbData);
        free(bin);
        return TRUE;
    }
    free(bin);
    return FALSE;
}

BOOL EncryptPassword(const char *plain, char *outEnc, int outSize) {
    DATA_BLOB in, out;
    in.pbData = (BYTE*)plain; in.cbData = (DWORD)strlen(plain);
    if (CryptProtectData(&in, L"OPC2Modbus", NULL, NULL, NULL, 0, &out)) {
        DWORD base64Len = 0;
        CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);
        if (base64Len <= (DWORD)outSize) {
            CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, outEnc, &base64Len);
            LocalFree(out.pbData);
            return TRUE;
        }
        LocalFree(out.pbData);
    }
    return FALSE;
}