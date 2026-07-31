#define _POSIX_C_SOURCE 200809L

#include "wirecommand/logging.h"
#include "wirecommand/server_tcp.h"
#include "wirecommand/socket_utils.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static volatile sig_atomic_t wc_tcp_stop_requested = 0;

static void wc_tcp_handle_stop_signal(int signal_number)
{
    (void)signal_number;
    wc_tcp_stop_requested = 1;
}

static int wc_tcp_install_stop_handler(int signal_number)
{
    struct sigaction action = {0};

    action.sa_handler = wc_tcp_handle_stop_signal;
    if (sigemptyset(&action.sa_mask) == -1) {
        return -1;
    }
    return sigaction(signal_number, &action, NULL);
}

static int wc_tcp_parse_port(const char *text, uint16_t *port)
{
    char *end;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || *end != '\0' || value == 0 ||
        value > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }
    *port = (uint16_t)value;
    return 0;
}

static int wc_tcp_parse_log_level(const char *text,
                                  enum wc_log_level *level)
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

static void wc_tcp_print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--bind IPv4_ADDRESS] [--port 1-65535] "
            "[--log-level error|warn|info|debug|trace]\n",
            program);
}

int main(int argument_count, char **arguments)
{
    const char *bind_address = "127.0.0.1";
    uint16_t port = 9090;
    enum wc_log_level log_level = WC_LOG_INFO;
    int index;

    for (index = 1; index < argument_count; ++index) {
        if (strcmp(arguments[index], "--bind") == 0 &&
            index + 1 < argument_count) {
            bind_address = arguments[++index];
        } else if (strcmp(arguments[index], "--port") == 0 &&
                   index + 1 < argument_count) {
            if (wc_tcp_parse_port(arguments[++index], &port) == -1) {
                wc_tcp_print_usage(arguments[0]);
                return 2;
            }
        } else if (strcmp(arguments[index], "--log-level") == 0 &&
                   index + 1 < argument_count) {
            if (wc_tcp_parse_log_level(arguments[++index], &log_level) == -1) {
                wc_tcp_print_usage(arguments[0]);
                return 2;
            }
        } else {
            wc_tcp_print_usage(arguments[0]);
            return 2;
        }
    }

    wc_log_set_level(log_level);
    if (wc_socket_ignore_sigpipe() == -1 ||
        wc_tcp_install_stop_handler(SIGINT) == -1 ||
        wc_tcp_install_stop_handler(SIGTERM) == -1) {
        perror("wirecommand-tcp: signal setup");
        return 1;
    }
    if (wc_server_tcp_run(bind_address, port, &wc_tcp_stop_requested) == -1) {
        perror("wirecommand-tcp");
        return 1;
    }
    return 0;
}
