#ifndef WIRECOMMAND_LOGGING_H
#define WIRECOMMAND_LOGGING_H

#include <stdarg.h>

enum wc_log_level {
    WC_LOG_ERROR = 0,
    WC_LOG_WARN,
    WC_LOG_INFO,
    WC_LOG_DEBUG,
    WC_LOG_TRACE
};

/* Return the stable uppercase name for level, or "UNKNOWN". */
const char *wc_log_level_name(enum wc_log_level level);

/*
 * Set or retrieve the process-wide minimum level. The default is INFO.
 * Set the level before starting worker threads; concurrent changes are not
 * supported by the intentionally lock-free logger.
 */
void wc_log_set_level(enum wc_log_level level);
enum wc_log_level wc_log_get_level(void);

/*
 * Emit one structured record to stderr when level is enabled.
 * component and event must be short, trusted identifiers. The optional
 * format string supplies bounded key=value context chosen by the caller.
 * Logging is best-effort, never changes errno, and owns none of its inputs.
 */
void wc_log(enum wc_log_level level, const char *component, const char *event,
            const char *format, ...);
void wc_log_v(enum wc_log_level level, const char *component,
              const char *event, const char *format, va_list arguments);

#endif
