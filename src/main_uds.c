#define _POSIX_C_SOURCE 200809L

#include "wirecommand/logging.h"
#include "wirecommand/server_uds.h"
#include "wirecommand/socket_utils.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static volatile sig_atomic_t wc_stop_requested = 0;

/* Signal handlers may safely assign a sig_atomic_t value. Cleanup happens later. */
static void wc_handle_stop_signal(int signal_number)
{
    (void)signal_number;
    wc_stop_requested = 1;
}

static int wc_install_stop_handler(int signal_number)
{
    struct sigaction action = {0};

    action.sa_handler = wc_handle_stop_signal;
    if (sigemptyset(&action.sa_mask) == -1) {
        return -1;
    }
    return sigaction(signal_number, &action, NULL);
}

static int wc_parse_log_level(const char *text, enum wc_log_level *level)
{
    static const char *const names[] = {"error", "warn", "info", "debug",
                                        "trace"};
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcasecmp(text, names[index]) == 0) {
            *level = (enum wc_log_level)index;
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

static void wc_print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--socket PATH] [--log-level "
            "error|warn|info|debug|trace]\n",
            program);
}

int main(int argument_count, char **arguments)
{
    const char *socket_path = "/tmp/wirecommand.sock";
    enum wc_log_level log_level = WC_LOG_INFO;
    int index;

    for (index = 1; index < argument_count; ++index) {
        if (strcmp(arguments[index], "--socket") == 0 &&
            index + 1 < argument_count) {
            socket_path = arguments[++index];
        } else if (strcmp(arguments[index], "--log-level") == 0 &&
                   index + 1 < argument_count) {
            if (wc_parse_log_level(arguments[++index], &log_level) == -1) {
                wc_print_usage(arguments[0]);
                return 2;
            }
        } else {
            wc_print_usage(arguments[0]);
            return 2;
        }
    }

    wc_log_set_level(log_level);
    if (wc_socket_ignore_sigpipe() == -1) {
        perror("wirecommand-uds: SIGPIPE setup");
        return 1;
    }
    if (wc_install_stop_handler(SIGINT) == -1 ||
        wc_install_stop_handler(SIGTERM) == -1) {
        perror("wirecommand-uds: stop signal setup");
        return 1;
    }

    if (wc_server_uds_run(socket_path, &wc_stop_requested) == -1) {
        perror("wirecommand-uds");
        return 1;
    }
    return 0;
}
