#include "logger.h"

#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *g_log_fp = NULL;
static LogLevel g_level = LOG_LEVEL_INFO;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *level_to_str(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default: return "INFO";
    }
}

static void format_timestamp(char *buf, size_t bufsz) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    // YYYY-MM-DD HH:MM:SS.mmm
    snprintf(buf, bufsz, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec,
             ts.tv_nsec / 1000000L);
}

static int ensure_dir_exists(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        errno = ENOTDIR;
        return -1;
    }
    if (mkdir(dir, 0700) == 0) return 0;
    return -1;
}

int log_init(const char *log_path, LogLevel level) {
    pthread_mutex_lock(&g_log_mutex);
    g_level = level;

    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    if (log_path && log_path[0] != '\0') {
        // Nếu đường dẫn có thư mục, tạo thư mục logs cơ bản
        // (đơn giản: tạo "logs" nếu prefix là logs/..)
        if (strncmp(log_path, "logs/", 5) == 0) {
            (void)ensure_dir_exists("logs");
        }

        g_log_fp = fopen(log_path, "ab");
        if (!g_log_fp) {
            pthread_mutex_unlock(&g_log_mutex);
            return -1;
        }
        // line-buffered
        setvbuf(g_log_fp, NULL, _IOLBF, 0);
    }

    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

void log_close() {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void log_write(LogLevel level, const char *fmt, ...) {
    if (level < g_level) return;

    char ts[64];
    format_timestamp(ts, sizeof(ts));

    pthread_t tid = pthread_self();

    pthread_mutex_lock(&g_log_mutex);

    FILE *out = (level >= LOG_LEVEL_ERROR) ? stderr : stdout;

    // Write to stdout/stderr
    fprintf(out, "[%s] [%s] [tid=%lu] ", ts, level_to_str(level), (unsigned long)tid);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fprintf(out, "\n");

    // Write to file if configured
    if (g_log_fp) {
        fprintf(g_log_fp, "[%s] [%s] [tid=%lu] ", ts, level_to_str(level), (unsigned long)tid);
        va_start(ap, fmt);
        vfprintf(g_log_fp, fmt, ap);
        va_end(ap);
        fprintf(g_log_fp, "\n");
    }

    pthread_mutex_unlock(&g_log_mutex);
}
