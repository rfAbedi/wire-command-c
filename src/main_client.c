#define _POSIX_C_SOURCE 200809L

#include "wirecommand/buffer.h"
#include "wirecommand/protocol.h"
#include "wirecommand/socket_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void wc_client_print_usage(const char *program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s [--socket PATH] PWD\n"
            "  %s [--socket PATH] LS DIRECTORY\n"
            "  %s [--socket PATH] CAT /ABSOLUTE/FILE\n"
            "  %s --host IPv4 [--port 1-65535] PWD|LS|CAT [ARGUMENT]\n",
            program, program, program, program);
}

static int wc_client_parse_command(const char *name,
                                   enum wc_request_type *type)
{
    if (strcmp(name, "LS") == 0) {
        *type = WC_REQUEST_LS;
    } else if (strcmp(name, "PWD") == 0) {
        *type = WC_REQUEST_PWD;
    } else if (strcmp(name, "CAT") == 0) {
        *type = WC_REQUEST_CAT;
    } else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int wc_client_connect_uds(const char *socket_path)
{
    struct sockaddr_un address = {0};
    int socket_fd;

    if (socket_path[0] == '\0' ||
        strlen(socket_path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        return -1;
    }
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1);

    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) ==
        -1) {
        int saved_errno = errno;

        (void)close(socket_fd);
        errno = saved_errno;
        return -1;
    }
    return socket_fd;
}

static int wc_client_parse_port(const char *text, uint16_t *port)
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

static int wc_client_connect_tcp(const char *host, uint16_t port)
{
    struct sockaddr_in address = {0};
    int socket_fd;
    int address_result;

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address_result = inet_pton(AF_INET, host, &address.sin_addr);
    if (address_result != 1) {
        if (address_result == 0) {
            errno = EINVAL;
        }
        return -1;
    }

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        return -1;
    }
    if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) ==
        -1) {
        int saved_errno = errno;

        (void)close(socket_fd);
        errno = saved_errno;
        return -1;
    }
    return socket_fd;
}

/* Read until the incremental parser has one complete response. */
static int wc_client_receive_response(int socket_fd)
{
    struct wc_buffer input;
    int result = -1;

    if (wc_buffer_init(&input,
                       UINT16_MAX + WC_PROTOCOL_RESPONSE_HEADER_SIZE) == -1) {
        return -1;
    }

    for (;;) {
        struct wc_response response;
        size_t consumed;
        enum wc_parse_result parse_result = wc_protocol_parse_response(
            input.data, input.length, &response, &consumed);

        if (parse_result == WC_PARSE_COMPLETE) {
            result = wc_socket_write_all(STDOUT_FILENO, response.data,
                                         response.data_length);
            break;
        }
        if (parse_result == WC_PARSE_INVALID ||
            input.length == input.max_size) {
            errno = EPROTO;
            break;
        }

        {
            unsigned char bytes[4096];
            size_t available = input.max_size - input.length;
            size_t read_size =
                available < sizeof(bytes) ? available : sizeof(bytes);
            ssize_t bytes_read = read(socket_fd, bytes, read_size);

            if (bytes_read > 0) {
                if (wc_buffer_append(&input, bytes, (size_t)bytes_read) == -1) {
                    break;
                }
            } else if (bytes_read == -1 && errno == EINTR) {
                continue;
            } else {
                if (bytes_read == 0) {
                    errno = EPIPE;
                }
                break;
            }
        }
    }

    wc_buffer_destroy(&input);
    return result;
}

int main(int argument_count, char **arguments)
{
    const char *socket_path = "/tmp/wirecommand.sock";
    const char *tcp_host = "127.0.0.1";
    uint16_t tcp_port = 9090;
    const char *command_name;
    const char *command_argument = NULL;
    enum wc_request_type request_type;
    struct wc_buffer request;
    size_t argument_length = 0;
    int argument_index = 1;
    int socket_option_seen = 0;
    int use_tcp = 0;
    int socket_fd;
    int result = 1;

    while (argument_index < argument_count) {
        const char *option = arguments[argument_index];

        if (strcmp(option, "--socket") == 0 &&
            argument_index + 1 < argument_count && !use_tcp) {
            socket_path = arguments[argument_index + 1];
            socket_option_seen = 1;
            argument_index += 2;
        } else if (strcmp(option, "--host") == 0 &&
                   argument_index + 1 < argument_count &&
                   !socket_option_seen) {
            tcp_host = arguments[argument_index + 1];
            use_tcp = 1;
            argument_index += 2;
        } else if (strcmp(option, "--port") == 0 &&
                   argument_index + 1 < argument_count &&
                   !socket_option_seen) {
            if (wc_client_parse_port(arguments[argument_index + 1],
                                     &tcp_port) == -1) {
                wc_client_print_usage(arguments[0]);
                return 2;
            }
            use_tcp = 1;
            argument_index += 2;
        } else {
            break;
        }
    }
    if (argument_index >= argument_count) {
        wc_client_print_usage(arguments[0]);
        return 2;
    }

    command_name = arguments[argument_index++];
    if (wc_client_parse_command(command_name, &request_type) == -1) {
        wc_client_print_usage(arguments[0]);
        return 2;
    }
    if (request_type == WC_REQUEST_PWD) {
        if (argument_index != argument_count) {
            wc_client_print_usage(arguments[0]);
            return 2;
        }
    } else {
        if (argument_index + 1 != argument_count) {
            wc_client_print_usage(arguments[0]);
            return 2;
        }
        command_argument = arguments[argument_index];
        argument_length = strlen(command_argument);
    }

    if (wc_buffer_init(&request, UINT16_MAX) == -1) {
        perror("wirecommand: encode request");
        return 1;
    }
    if (wc_protocol_encode_request(&request, request_type, command_argument,
                                   argument_length) == -1) {
        perror("wirecommand: encode request");
        wc_buffer_destroy(&request);
        return 1;
    }
    if (wc_socket_ignore_sigpipe() == -1) {
        perror("wirecommand: SIGPIPE setup");
        wc_buffer_destroy(&request);
        return 1;
    }

    if (use_tcp) {
        socket_fd = wc_client_connect_tcp(tcp_host, tcp_port);
    } else {
        socket_fd = wc_client_connect_uds(socket_path);
    }
    if (socket_fd == -1) {
        perror("wirecommand: connect");
        wc_buffer_destroy(&request);
        return 1;
    }
    if (wc_socket_write_all(socket_fd, request.data, request.length) == -1) {
        perror("wirecommand: send request");
    } else if (wc_client_receive_response(socket_fd) == -1) {
        perror("wirecommand: receive response");
    } else {
        result = 0;
    }

    if (close(socket_fd) == -1 && result == 0) {
        perror("wirecommand: close");
        result = 1;
    }
    wc_buffer_destroy(&request);
    return result;
}
