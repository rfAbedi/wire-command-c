#define _POSIX_C_SOURCE 200809L

#include "wirecommand/buffer.h"
#include "wirecommand/protocol.h"
#include "wirecommand/socket_utils.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct wc_tcp_fixture {
    pid_t process_id;
    uint16_t port;
    int client_fd;
};

static volatile sig_atomic_t wc_active_server_pid = -1;
static const char *wc_tcp_client_program;

#define WC_TCP_ASSERT(condition)                                            \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                  \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static void wc_tcp_short_pause(void)
{
    struct timespec delay = {0, 10000000};

    while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
    }
}

static int wc_tcp_find_free_port(uint16_t *port)
{
    struct sockaddr_in address = {0};
    socklen_t address_size = sizeof(address);
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        getsockname(socket_fd, (struct sockaddr *)&address,
                    &address_size) == -1) {
        int saved_errno = errno;

        (void)close(socket_fd);
        errno = saved_errno;
        return -1;
    }
    *port = ntohs(address.sin_port);
    return close(socket_fd);
}

static int wc_tcp_connect(uint16_t port)
{
    struct sockaddr_in address = {0};
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (client_fd == -1) {
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(client_fd, (struct sockaddr *)&address, sizeof(address)) ==
        -1) {
        int saved_errno = errno;

        (void)close(client_fd);
        errno = saved_errno;
        return -1;
    }
    return client_fd;
}

static int wc_tcp_wait_for_connection(uint16_t port)
{
    int attempt;

    for (attempt = 0; attempt < 300; ++attempt) {
        int client_fd = wc_tcp_connect(port);

        if (client_fd != -1) {
            return client_fd;
        }
        if (errno != ECONNREFUSED) {
            return -1;
        }
        wc_tcp_short_pause();
    }
    errno = ETIMEDOUT;
    return -1;
}

static int wc_tcp_wait_for_process(pid_t process_id, int *status)
{
    int attempt;

    for (attempt = 0; attempt < 300; ++attempt) {
        pid_t wait_result = waitpid(process_id, status, WNOHANG);

        if (wait_result == process_id) {
            return 0;
        }
        if (wait_result == -1 && errno != EINTR) {
            return -1;
        }
        wc_tcp_short_pause();
    }
    errno = ETIMEDOUT;
    return -1;
}

static void wc_tcp_force_stop_server(void)
{
    pid_t process_id = (pid_t)wc_active_server_pid;
    int status;

    if (process_id <= 0) {
        return;
    }
    (void)kill(process_id, SIGTERM);
    if (wc_tcp_wait_for_process(process_id, &status) == -1) {
        (void)kill(process_id, SIGKILL);
        while (waitpid(process_id, NULL, 0) == -1 && errno == EINTR) {
        }
    }
    wc_active_server_pid = -1;
}

static void wc_tcp_handle_timeout(int signal_number)
{
    pid_t process_id = (pid_t)wc_active_server_pid;

    (void)signal_number;
    if (process_id > 0) {
        (void)kill(process_id, SIGKILL);
    }
    _exit(124);
}

static int wc_tcp_start_server(struct wc_tcp_fixture *fixture,
                               const char *server_program)
{
    char port_text[16];

    fixture->process_id = -1;
    fixture->client_fd = -1;
    if (wc_tcp_find_free_port(&fixture->port) == -1 ||
        snprintf(port_text, sizeof(port_text), "%u",
                 (unsigned int)fixture->port) < 0) {
        return -1;
    }

    fixture->process_id = fork();
    if (fixture->process_id == -1) {
        return -1;
    }
    if (fixture->process_id == 0) {
        execl(server_program, server_program, "--bind", "127.0.0.1",
              "--port", port_text, "--log-level", "error", (char *)NULL);
        _exit(127);
    }
    wc_active_server_pid = (sig_atomic_t)fixture->process_id;
    fixture->client_fd = wc_tcp_wait_for_connection(fixture->port);
    if (fixture->client_fd == -1) {
        int saved_errno = errno;

        wc_tcp_force_stop_server();
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int wc_tcp_stop_server(struct wc_tcp_fixture *fixture)
{
    int status;

    if (fixture->client_fd != -1) {
        if (close(fixture->client_fd) == -1) {
            return -1;
        }
        fixture->client_fd = -1;
    }
    if (kill(fixture->process_id, SIGTERM) == -1 ||
        wc_tcp_wait_for_process(fixture->process_id, &status) == -1) {
        return -1;
    }
    wc_active_server_pid = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = ECHILD;
        return -1;
    }
    return 0;
}

static int wc_tcp_make_pwd_request(struct wc_buffer *request)
{
    if (wc_buffer_init(request, UINT16_MAX) == -1) {
        return -1;
    }
    if (wc_protocol_encode_request(request, WC_REQUEST_PWD, NULL, 0) == -1) {
        wc_buffer_destroy(request);
        return -1;
    }
    return 0;
}

static int wc_tcp_receive_response(int client_fd, struct wc_buffer *input,
                                   struct wc_response *response)
{
    for (;;) {
        size_t consumed;
        enum wc_parse_result parse_result = wc_protocol_parse_response(
            input->data, input->length, response, &consumed);

        if (parse_result == WC_PARSE_COMPLETE) {
            return 0;
        }
        if (parse_result == WC_PARSE_INVALID ||
            input->length == input->max_size) {
            errno = EPROTO;
            return -1;
        }

        {
            unsigned char bytes[4096];
            ssize_t bytes_read = read(client_fd, bytes, sizeof(bytes));

            if (bytes_read > 0) {
                if (wc_buffer_append(input, bytes, (size_t)bytes_read) == -1) {
                    return -1;
                }
            } else if (bytes_read == -1 && errno == EINTR) {
                continue;
            } else {
                if (bytes_read == 0) {
                    errno = EPIPE;
                }
                return -1;
            }
        }
    }
}

static int wc_tcp_response_is_pwd(const struct wc_response *response)
{
    char current_directory[4096];
    size_t directory_length;

    if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        return 0;
    }
    directory_length = strlen(current_directory);
    return response->data_length == directory_length &&
           memcmp(response->data, current_directory, directory_length) == 0;
}

static int wc_tcp_count_process_entries(pid_t process_id,
                                        const char *entry_name,
                                        size_t *count)
{
    char path[64];
    DIR *directory;
    struct dirent *entry;

    if (snprintf(path, sizeof(path), "/proc/%ld/%s", (long)process_id,
                 entry_name) < 0) {
        return -1;
    }
    directory = opendir(path);
    if (directory == NULL) {
        return -1;
    }
    *count = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            ++*count;
        }
    }
    return closedir(directory);
}

static int wc_tcp_count_threads(pid_t process_id, size_t *count)
{
    return wc_tcp_count_process_entries(process_id, "task", count);
}

static int wc_tcp_count_descriptors(pid_t process_id, size_t *count)
{
    return wc_tcp_count_process_entries(process_id, "fd", count);
}

static int test_tcp_fragmented_request_returns_response(
    const char *server_program)
{
    struct wc_tcp_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer input;
    struct wc_response response;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(wc_tcp_make_pwd_request(&request) == 0);
    WC_TCP_ASSERT(wc_buffer_init(&input, UINT16_MAX + 2) == 0);
    WC_TCP_ASSERT(wc_socket_write_all(fixture.client_fd, request.data, 2) == 0);
    wc_tcp_short_pause();
    WC_TCP_ASSERT(wc_socket_write_all(fixture.client_fd, request.data + 2,
                                      request.length - 2) == 0);
    WC_TCP_ASSERT(wc_tcp_receive_response(fixture.client_fd, &input,
                                          &response) == 0);
    WC_TCP_ASSERT(wc_tcp_response_is_pwd(&response));
    WC_TCP_ASSERT(wc_buffer_consume(&input, response.message_length) == 0);

    wc_buffer_destroy(&input);
    wc_buffer_destroy(&request);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

static int test_tcp_coalesced_requests_return_two_responses(
    const char *server_program)
{
    struct wc_tcp_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer input;
    struct wc_response first;
    struct wc_response second;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(wc_tcp_make_pwd_request(&request) == 0);
    WC_TCP_ASSERT(wc_protocol_encode_request(&request, WC_REQUEST_PWD, NULL,
                                             0) == 0);
    WC_TCP_ASSERT(wc_buffer_init(&input, UINT16_MAX + 2) == 0);
    WC_TCP_ASSERT(wc_socket_write_all(fixture.client_fd, request.data,
                                      request.length) == 0);
    WC_TCP_ASSERT(wc_tcp_receive_response(fixture.client_fd, &input,
                                          &first) == 0);
    WC_TCP_ASSERT(wc_tcp_response_is_pwd(&first));
    WC_TCP_ASSERT(wc_buffer_consume(&input, first.message_length) == 0);
    WC_TCP_ASSERT(wc_tcp_receive_response(fixture.client_fd, &input,
                                          &second) == 0);
    WC_TCP_ASSERT(wc_tcp_response_is_pwd(&second));
    WC_TCP_ASSERT(wc_buffer_consume(&input, second.message_length) == 0);

    wc_buffer_destroy(&input);
    wc_buffer_destroy(&request);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

static int test_tcp_clients_have_independent_workers(
    const char *server_program)
{
    struct wc_tcp_fixture fixture;
    struct wc_buffer request;
    int clients[3];
    int index;
    int attempt;
    size_t thread_count = 0;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(wc_tcp_make_pwd_request(&request) == 0);
    for (index = 0; index < 3; ++index) {
        clients[index] = wc_tcp_connect(fixture.port);
        WC_TCP_ASSERT(clients[index] != -1);
        WC_TCP_ASSERT(wc_socket_write_all(clients[index], request.data,
                                          request.length) == 0);
    }
    for (index = 0; index < 3; ++index) {
        struct wc_buffer input;
        struct wc_response response;

        WC_TCP_ASSERT(wc_buffer_init(&input, UINT16_MAX + 2) == 0);
        WC_TCP_ASSERT(wc_tcp_receive_response(clients[index], &input,
                                              &response) == 0);
        WC_TCP_ASSERT(wc_tcp_response_is_pwd(&response));
        WC_TCP_ASSERT(wc_buffer_consume(&input, response.message_length) == 0);
        wc_buffer_destroy(&input);
    }

    for (attempt = 0; attempt < 100; ++attempt) {
        WC_TCP_ASSERT(wc_tcp_count_threads(fixture.process_id,
                                           &thread_count) == 0);
        if (thread_count >= 5) {
            break;
        }
        wc_tcp_short_pause();
    }
    WC_TCP_ASSERT(thread_count >= 5);

    for (index = 0; index < 3; ++index) {
        WC_TCP_ASSERT(close(clients[index]) == 0);
    }
    wc_buffer_destroy(&request);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

static int test_tcp_invalid_client_does_not_stop_server(
    const char *server_program)
{
    struct wc_tcp_fixture fixture;
    unsigned char invalid_request[] = {0, 6, 0, 99, 0, 0};
    struct pollfd invalid_client;
    struct wc_buffer request;
    struct wc_buffer input;
    struct wc_response response;
    int valid_client;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(wc_socket_write_all(fixture.client_fd, invalid_request,
                                      sizeof(invalid_request)) == 0);
    invalid_client.fd = fixture.client_fd;
    invalid_client.events = POLLIN | POLLHUP;
    invalid_client.revents = 0;
    WC_TCP_ASSERT(poll(&invalid_client, 1, 2000) > 0);

    valid_client = wc_tcp_connect(fixture.port);
    WC_TCP_ASSERT(valid_client != -1);
    WC_TCP_ASSERT(wc_tcp_make_pwd_request(&request) == 0);
    WC_TCP_ASSERT(wc_buffer_init(&input, UINT16_MAX + 2) == 0);
    WC_TCP_ASSERT(wc_socket_write_all(valid_client, request.data,
                                      request.length) == 0);
    WC_TCP_ASSERT(wc_tcp_receive_response(valid_client, &input, &response) ==
                  0);
    WC_TCP_ASSERT(wc_tcp_response_is_pwd(&response));
    WC_TCP_ASSERT(wc_buffer_consume(&input, response.message_length) == 0);

    WC_TCP_ASSERT(close(valid_client) == 0);
    wc_buffer_destroy(&input);
    wc_buffer_destroy(&request);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

static int test_tcp_shutdown_stops_active_workers(const char *server_program)
{
    struct wc_tcp_fixture fixture;
    int extra_client;
    int status;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    extra_client = wc_tcp_connect(fixture.port);
    WC_TCP_ASSERT(extra_client != -1);
    WC_TCP_ASSERT(kill(fixture.process_id, SIGTERM) == 0);
    WC_TCP_ASSERT(wc_tcp_wait_for_process(fixture.process_id, &status) == 0);
    wc_active_server_pid = -1;
    WC_TCP_ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    WC_TCP_ASSERT(close(extra_client) == 0);
    WC_TCP_ASSERT(close(fixture.client_fd) == 0);
    fixture.client_fd = -1;
    return 0;
}

static int test_tcp_command_line_client_receives_response(
    const char *server_program)
{
    struct wc_tcp_fixture fixture;
    char port_text[16];
    char output[4096];
    char current_directory[4096];
    int output_pipe[2];
    pid_t client_process;
    size_t output_size = 0;
    int client_status;
    pid_t wait_result;

    WC_TCP_ASSERT(wc_tcp_client_program != NULL);
    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(snprintf(port_text, sizeof(port_text), "%u",
                           (unsigned int)fixture.port) > 0);
    WC_TCP_ASSERT(pipe(output_pipe) == 0);

    client_process = fork();
    WC_TCP_ASSERT(client_process != -1);
    if (client_process == 0) {
        (void)close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) == -1) {
            _exit(126);
        }
        (void)close(output_pipe[1]);
        execl(wc_tcp_client_program, wc_tcp_client_program, "--host",
              "127.0.0.1", "--port", port_text, "--log-level", "error",
              "PWD", (char *)NULL);
        _exit(127);
    }

    WC_TCP_ASSERT(close(output_pipe[1]) == 0);
    while (output_size < sizeof(output)) {
        ssize_t bytes_read =
            read(output_pipe[0], output + output_size,
                 sizeof(output) - output_size);

        if (bytes_read > 0) {
            output_size += (size_t)bytes_read;
        } else if (bytes_read == -1 && errno == EINTR) {
            continue;
        } else {
            WC_TCP_ASSERT(bytes_read == 0);
            break;
        }
    }
    WC_TCP_ASSERT(close(output_pipe[0]) == 0);
    do {
        wait_result = waitpid(client_process, &client_status, 0);
    } while (wait_result == -1 && errno == EINTR);
    WC_TCP_ASSERT(wait_result == client_process);
    WC_TCP_ASSERT(WIFEXITED(client_status) && WEXITSTATUS(client_status) == 0);
    WC_TCP_ASSERT(getcwd(current_directory, sizeof(current_directory)) != NULL);
    WC_TCP_ASSERT(output_size == strlen(current_directory));
    WC_TCP_ASSERT(memcmp(output, current_directory, output_size) == 0);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

static int test_tcp_half_closed_client_receives_response(
    const char *server_program)
{
    struct wc_tcp_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer input;
    struct wc_response response;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(wc_tcp_make_pwd_request(&request) == 0);
    WC_TCP_ASSERT(wc_buffer_init(&input, UINT16_MAX + 2) == 0);
    WC_TCP_ASSERT(wc_socket_write_all(fixture.client_fd, request.data,
                                      request.length) == 0);
    WC_TCP_ASSERT(shutdown(fixture.client_fd, SHUT_WR) == 0);
    WC_TCP_ASSERT(wc_tcp_receive_response(fixture.client_fd, &input,
                                          &response) == 0);
    WC_TCP_ASSERT(wc_tcp_response_is_pwd(&response));

    wc_buffer_destroy(&input);
    wc_buffer_destroy(&request);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

static int test_tcp_many_clients_receive_every_response(
    const char *server_program)
{
    enum { CLIENT_COUNT = 8, REQUEST_COUNT = 20 };
    struct wc_tcp_fixture fixture;
    struct wc_buffer requests;
    struct wc_buffer inputs[CLIENT_COUNT];
    int clients[CLIENT_COUNT];
    int client_index;
    int request_index;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(wc_buffer_init(&requests, UINT16_MAX) == 0);
    for (request_index = 0; request_index < REQUEST_COUNT; ++request_index) {
        WC_TCP_ASSERT(wc_protocol_encode_request(
                          &requests, WC_REQUEST_PWD, NULL, 0) == 0);
    }

    for (client_index = 0; client_index < CLIENT_COUNT; ++client_index) {
        clients[client_index] = wc_tcp_connect(fixture.port);
        WC_TCP_ASSERT(clients[client_index] != -1);
        WC_TCP_ASSERT(wc_buffer_init(&inputs[client_index], UINT16_MAX + 2) ==
                      0);
        WC_TCP_ASSERT(wc_socket_write_all(clients[client_index], requests.data,
                                          requests.length) == 0);
    }

    for (client_index = 0; client_index < CLIENT_COUNT; ++client_index) {
        for (request_index = 0; request_index < REQUEST_COUNT;
             ++request_index) {
            struct wc_response response;

            WC_TCP_ASSERT(wc_tcp_receive_response(
                              clients[client_index], &inputs[client_index],
                              &response) == 0);
            WC_TCP_ASSERT(wc_tcp_response_is_pwd(&response));
            WC_TCP_ASSERT(wc_buffer_consume(
                              &inputs[client_index],
                              response.message_length) == 0);
        }
        WC_TCP_ASSERT(close(clients[client_index]) == 0);
        wc_buffer_destroy(&inputs[client_index]);
    }

    wc_buffer_destroy(&requests);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

static int test_tcp_repeated_connections_release_resources(
    const char *server_program)
{
    struct wc_tcp_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer input;
    struct wc_response response;
    size_t initial_descriptors;
    size_t initial_threads;
    size_t final_descriptors = 0;
    size_t thread_count = 0;
    int connection;
    int attempt;

    WC_TCP_ASSERT(wc_tcp_start_server(&fixture, server_program) == 0);
    WC_TCP_ASSERT(wc_tcp_make_pwd_request(&request) == 0);
    WC_TCP_ASSERT(wc_buffer_init(&input, UINT16_MAX + 2) == 0);
    WC_TCP_ASSERT(wc_socket_write_all(fixture.client_fd, request.data,
                                      request.length) == 0);
    WC_TCP_ASSERT(wc_tcp_receive_response(fixture.client_fd, &input,
                                          &response) == 0);
    WC_TCP_ASSERT(wc_tcp_response_is_pwd(&response));
    wc_buffer_destroy(&input);
    wc_buffer_destroy(&request);
    WC_TCP_ASSERT(wc_tcp_count_descriptors(fixture.process_id,
                                           &initial_descriptors) == 0);
    WC_TCP_ASSERT(wc_tcp_count_threads(fixture.process_id,
                                       &initial_threads) == 0);

    for (connection = 0; connection < 50; ++connection) {
        int client_fd = wc_tcp_connect(fixture.port);

        WC_TCP_ASSERT(client_fd != -1);
        WC_TCP_ASSERT(close(client_fd) == 0);
    }

    for (attempt = 0; attempt < 300; ++attempt) {
        WC_TCP_ASSERT(wc_tcp_count_descriptors(fixture.process_id,
                                               &final_descriptors) == 0);
        WC_TCP_ASSERT(wc_tcp_count_threads(fixture.process_id,
                                           &thread_count) == 0);
        if (final_descriptors <= initial_descriptors &&
            thread_count <= initial_threads) {
            break;
        }
        wc_tcp_short_pause();
    }
    WC_TCP_ASSERT(final_descriptors <= initial_descriptors);
    WC_TCP_ASSERT(thread_count <= initial_threads);
    WC_TCP_ASSERT(wc_tcp_stop_server(&fixture) == 0);
    return 0;
}

int main(int argument_count, char **arguments)
{
    struct wc_tcp_test {
        const char *name;
        int (*function)(const char *server_program);
    } tests[] = {
        {"test_tcp_fragmented_request_returns_response",
         test_tcp_fragmented_request_returns_response},
        {"test_tcp_coalesced_requests_return_two_responses",
         test_tcp_coalesced_requests_return_two_responses},
        {"test_tcp_clients_have_independent_workers",
         test_tcp_clients_have_independent_workers},
        {"test_tcp_invalid_client_does_not_stop_server",
         test_tcp_invalid_client_does_not_stop_server},
        {"test_tcp_shutdown_stops_active_workers",
         test_tcp_shutdown_stops_active_workers},
        {"test_tcp_command_line_client_receives_response",
         test_tcp_command_line_client_receives_response},
        {"test_tcp_half_closed_client_receives_response",
         test_tcp_half_closed_client_receives_response},
        {"test_tcp_many_clients_receive_every_response",
         test_tcp_many_clients_receive_every_response},
        {"test_tcp_repeated_connections_release_resources",
         test_tcp_repeated_connections_release_resources},
    };
    struct sigaction timeout_action = {0};
    size_t failures = 0;
    size_t index;

    if (argument_count != 3) {
        fprintf(stderr, "Usage: %s SERVER_PROGRAM CLIENT_PROGRAM\n",
                arguments[0]);
        return 2;
    }
    wc_tcp_client_program = arguments[2];
    timeout_action.sa_handler = wc_tcp_handle_timeout;
    WC_TCP_ASSERT(sigemptyset(&timeout_action.sa_mask) == 0);
    WC_TCP_ASSERT(sigaction(SIGALRM, &timeout_action, NULL) == 0);
    alarm(30);

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int test_result = tests[index].function(arguments[1]);

        wc_tcp_force_stop_server();
        if (test_result == 0) {
            printf("ok %zu - %s\n", index + 1, tests[index].name);
        } else {
            printf("not ok %zu - %s\n", index + 1, tests[index].name);
            ++failures;
        }
    }
    alarm(0);
    printf("1..%zu\n", sizeof(tests) / sizeof(tests[0]));
    return failures == 0 ? 0 : 1;
}
