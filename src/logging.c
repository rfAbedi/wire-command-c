#define _POSIX_C_SOURCE 200809L

#include "wirecommand/logging.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>

enum {
    WC_LOG_TIMESTAMP_SIZE = 40,
    WC_LOG_MESSAGE_SIZE = 2048
};

static enum wc_log_level wc_minimum_level = WC_LOG_INFO;

static int wc_log_level_valid(enum wc_log_level level)
{
    return level >= WC_LOG_ERROR && level <= WC_LOG_TRACE;
}

const char *wc_log_level_name(enum wc_log_level level)
{
    static const char *const names[] = {"ERROR", "WARN", "INFO", "DEBUG",
                                        "TRACE"};

    if (!wc_log_level_valid(level)) {
        return "UNKNOWN";
    }
    return names[level];
}

void wc_log_set_level(enum wc_log_level level)
{
    if (wc_log_level_valid(level)) {
        wc_minimum_level = level;
    }
}

enum wc_log_level wc_log_get_level(void)
{
    return wc_minimum_level;
}

static int wc_log_timestamp(char *destination, size_t size)
{
    time_t now = time(NULL);
    struct tm local_time;

    if (now == (time_t)-1) {
        return -1;
    }

    if (localtime_r(&now, &local_time) == NULL) {
        return -1;
    }

    if (strftime(destination, size, "%Y-%m-%d %H:%M:%S",
                 &local_time) == 0) {
        return -1;
    }

    return 0;
}

void wc_log_v(enum wc_log_level level, const char *component,
              const char *event, const char *format, va_list arguments)
{
    int saved_errno = errno;
    char timestamp[WC_LOG_TIMESTAMP_SIZE];
    char context[WC_LOG_MESSAGE_SIZE];
    int has_context = 0;

    if (!wc_log_level_valid(level) || level > wc_minimum_level ||
        component == NULL || event == NULL) {
        errno = saved_errno;
        return;
    }

    if (format != NULL && format[0] != '\0') {
        int result = vsnprintf(context, sizeof(context), format, arguments);
        has_context = result > 0;
    }

    if (wc_log_timestamp(timestamp, sizeof(timestamp)) == -1) {
        errno = saved_errno;
        return;
    }

    (void)fprintf(stderr, "[%s] %-5s %s: event=%s%s%s\n", timestamp,
                  wc_log_level_name(level), component, event,
                  has_context ? " " : "", has_context ? context : "");
    errno = saved_errno;
}

void wc_log(enum wc_log_level level, const char *component, const char *event,
            const char *format, ...)
{
    int saved_errno = errno;
    va_list arguments;

    va_start(arguments, format);
    wc_log_v(level, component, event, format, arguments);
    va_end(arguments);
    errno = saved_errno;
}
