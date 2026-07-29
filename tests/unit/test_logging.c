#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "wirecommand/logging.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct stderr_capture {
    int saved_stderr;
    FILE *temporary_file;
};

/*
 * Redirect stderr to a temporary file so a test can inspect one log record.
 * The caller must finish the capture with capture_stderr_end().
 */
static int capture_stderr_begin(struct stderr_capture *capture)
{
    if (fflush(stderr) == EOF) {
        return -1;
    }

    capture->saved_stderr = dup(STDERR_FILENO);
    if (capture->saved_stderr == -1) {
        return -1;
    }

    capture->temporary_file = tmpfile();
    if (capture->temporary_file == NULL) {
        int saved_errno = errno;

        (void)close(capture->saved_stderr);
        errno = saved_errno;
        return -1;
    }

    if (dup2(fileno(capture->temporary_file), STDERR_FILENO) == -1) {
        int saved_errno = errno;

        (void)fclose(capture->temporary_file);
        (void)close(capture->saved_stderr);
        errno = saved_errno;
        return -1;
    }

    return 0;
}

/*
 * Restore stderr and copy the captured text into the caller's buffer.
 * This helper keeps file-descriptor handling out of the behavioral tests.
 */
static int capture_stderr_end(struct stderr_capture *capture, char *output,
                              size_t capacity)
{
    size_t bytes_read;
    int restore_failed = 0;

    if (fflush(stderr) == EOF) {
        restore_failed = 1;
    }
    if (dup2(capture->saved_stderr, STDERR_FILENO) == -1) {
        restore_failed = 1;
    }
    if (close(capture->saved_stderr) == -1) {
        restore_failed = 1;
    }
    if (restore_failed) {
        (void)fclose(capture->temporary_file);
        return -1;
    }
    if (capacity == 0) {
        (void)fclose(capture->temporary_file);
        errno = EINVAL;
        return -1;
    }
    if (fseek(capture->temporary_file, 0L, SEEK_SET) == -1) {
        (void)fclose(capture->temporary_file);
        return -1;
    }

    bytes_read = fread(output, 1, capacity - 1, capture->temporary_file);
    output[bytes_read] = '\0';

    if (ferror(capture->temporary_file)) {
        (void)fclose(capture->temporary_file);
        return -1;
    }
    return fclose(capture->temporary_file);
}

int test_logging_set_level_changes_current_level(void)
{
    wc_log_set_level(WC_LOG_DEBUG);
    WC_TEST_ASSERT(wc_log_get_level() == WC_LOG_DEBUG);

    wc_log_set_level(WC_LOG_INFO);
    WC_TEST_ASSERT(wc_log_get_level() == WC_LOG_INFO);
    return 0;
}

int test_logging_level_name_returns_readable_name(void)
{
    WC_TEST_ASSERT_STRING_EQUAL("ERROR", wc_log_level_name(WC_LOG_ERROR));
    WC_TEST_ASSERT_STRING_EQUAL("DEBUG", wc_log_level_name(WC_LOG_DEBUG));
    return 0;
}

int test_logging_invalid_level_name_returns_unknown(void)
{
    WC_TEST_ASSERT_STRING_EQUAL("UNKNOWN",
                                wc_log_level_name((enum wc_log_level)99));
    return 0;
}

int test_logging_disabled_record_is_not_emitted(void)
{
    struct stderr_capture capture;
    char output[128];

    wc_log_set_level(WC_LOG_ERROR);

    WC_TEST_ASSERT(capture_stderr_begin(&capture) == 0);
    wc_log(WC_LOG_DEBUG, "logging", "hidden", "value=%d", 7);
    WC_TEST_ASSERT(capture_stderr_end(&capture, output, sizeof(output)) == 0);

    WC_TEST_ASSERT_STRING_EQUAL("", output);
    wc_log_set_level(WC_LOG_INFO);
    return 0;
}

int test_logging_record_has_structured_fields(void)
{
    struct stderr_capture capture;
    char output[512];

    wc_log_set_level(WC_LOG_DEBUG);

    WC_TEST_ASSERT(capture_stderr_begin(&capture) == 0);
    wc_log(WC_LOG_INFO, "test", "record", "client=fd:7");
    WC_TEST_ASSERT(capture_stderr_end(&capture, output, sizeof(output)) == 0);

    WC_TEST_ASSERT(output[0] != '\0');
    WC_TEST_ASSERT(strstr(output, "] INFO  test: event=record client=fd:7\n") !=
                   NULL);
    wc_log_set_level(WC_LOG_INFO);
    return 0;
}

int test_logging_call_preserves_errno(void)
{
    struct stderr_capture capture;
    char output[512];

    wc_log_set_level(WC_LOG_INFO);

    WC_TEST_ASSERT(capture_stderr_begin(&capture) == 0);
    errno = ENOENT;
    wc_log(WC_LOG_ERROR, "test", "errno", "value=%d", errno);
    WC_TEST_ASSERT(errno == ENOENT);

    WC_TEST_ASSERT(capture_stderr_end(&capture, output, sizeof(output)) == 0);
    return 0;
}
