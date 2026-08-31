#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdint.h>

typedef struct {
    int  tcp_port;
    int  slave_id;
    int  refresh_ms;
    char mapping_path[MAX_PATH];
    int autoStart;          // 新增：0 = 不自动启动，1 = 开机自启动
} AppConfig;

void LoadConfig(AppConfig *cfg);
void SaveConfig(const AppConfig *cfg);

// DPAPI 密码加密/解密
BOOL DecryptPassword(const char *encBase64, char *outPlain, int outSize);
BOOL EncryptPassword(const char *plain, char *outEnc, int outSize);

extern AppConfig g_cfg;

#endif