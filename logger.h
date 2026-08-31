#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

// 初始化日志系统（创建日志目录，清理过期日志）
int Logger_Init(const char *logDir);

// 写入一条日志，格式：[2025-06-23 14:30:00] 消息内容
void Logger_Write(const char *format, ...);

// 关闭日志系统
void Logger_Close(void);

#ifdef __cplusplus
}
#endif

#endif