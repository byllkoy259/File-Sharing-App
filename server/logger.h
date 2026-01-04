#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

// Khởi tạo logger (mở file log). Nếu log_path == NULL -> chỉ log ra stdout/stderr.
int log_init(const char *log_path, LogLevel level);
void log_close();

void log_write(LogLevel level, const char *fmt, ...);

// Convenience macros
#define LOG_DEBUG(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_write(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  log_write(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_ERROR(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif
