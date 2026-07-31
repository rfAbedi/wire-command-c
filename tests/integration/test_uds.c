#define _POSIX_C_SOURCE 200809L

#include "wirecommand/buffer.h"
#include "wirecommand/protocol.h"

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct wc_server_fixture {
    pid_t process_id;
    int client_fd;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
};

static volatile sig_atomic_t wc_active_server_pid = -1;

#define WC_INTEGRATION_ASSERT(condition)                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                  \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static void wc_short_pause(void)
{
    struct timespec delay = {0, 10000000};

    while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
    }
}

static int wc_connect_to_server(const char *socket_path)
{
    struct sockaddr_un address = {0};
    int client_fd;

    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd == -1) {
        return -1;
    }

    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1);
    if (connect(client_fd, (struct sockaddr *)&address, sizeof(address)) ==
        -1) {
        int saved_errno = errno;

        (void)close(client_fd);
        errno = saved_errno;
        return -1;
    }
    return client_fd;
}

static int wc_wait_for_connection(const char *socket_path)
{
    int attempt;

    for (attempt = 0; attempt < 300; ++attempt) {
        int client_fd = wc_connect_to_server(socket_path);

        if (client_fd != -1) {
            return client_fd;
        }
        if (errno != ENOENT && errno != ECONNREFUSED) {
            return -1;
        }
        wc_short_pause();
    }
    errno = ETIMEDOUT;
    return -1;
}

static int wc_wait_for_process(pid_t process_id, int *status)
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
        wc_short_pause();
    }
    errno = ETIMEDOUT;
    return -1;
}

/* Ensure a failed test cannot leave its real server running. */
static void wc_force_stop_active_server(void)
{
    pid_t process_id = (pid_t)wc_active_server_pid;
    int status;

    if (process_id <= 0) {
        return;
    }
    (void)kill(process_id, SIGTERM);
    if (wc_wait_for_process(process_id, &status) == -1) {
        (void)kill(process_id, SIGKILL);
        while (waitpid(process_id, NULL, 0) == -1 && errno == EINTR) {
        }
    }
    wc_active_server_pid = -1;
}

static void wc_handle_test_timeout(int signal_number)
{
    pid_t process_id = (pid_t)wc_active_server_pid;

    (void)signal_number;
    if (process_id > 0) {
        (void)kill(process_id, SIGKILL);
    }
    _exit(124);
}

static int wc_start_server(struct wc_server_fixture *fixture,
                           const char *server_program, int test_number)
{
    int path_length;

    fixture->process_id = -1;
    fixture->client_fd = -1;
    path_length = snprintf(fixture->socket_path, sizeof(fixture->socket_path),
                           "/tmp/wirecommand-%ld-%d.sock", (long)getpid(),
                           test_number);
    if (path_length < 0 ||
        (size_t)path_length >= sizeof(fixture->socket_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    fixture->process_id = fork();
    if (fixture->process_id == -1) {
        return -1;
    }
    if (fixture->process_id == 0) {
        execl(server_program, server_program, "--socket", fixture->socket_path,
              "--log-level", "error", (char *)NULL);
        _exit(127);
    }
    wc_active_server_pid = (sig_atomic_t)fixture->process_id;

    fixture->client_fd = wc_wait_for_connection(fixture->socket_path);
    if (fixture->client_fd == -1) {
        int saved_errno = errno;
        int status;

        (void)kill(fixture->process_id, SIGTERM);
        (void)wc_wait_for_process(fixture->process_id, &status);
        wc_active_server_pid = -1;
        (void)unlink(fixture->socket_path);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int wc_stop_server(struct wc_server_fixture *fixture)
{
    int status;

    if (fixture->client_fd != -1) {
        if (close(fixture->client_fd) == -1) {
            return -1;
        }
        fixture->client_fd = -1;
    }
    if (kill(fixture->process_id, SIGTERM) == -1) {
        return -1;
    }
    if (wc_wait_for_process(fixture->process_id, &status) == -1) {
        (void)kill(fixture->process_id, SIGKILL);
        (void)waitpid(fixture->process_id, NULL, 0);
        wc_active_server_pid = -1;
        return -1;
    }
    wc_active_server_pid = -1;
    fixture->process_id = -1;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = ECHILD;
        return -1;
    }
    if (access(fixture->socket_path, F_OK) != -1 || errno != ENOENT) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

static int wc_write_all(int descriptor, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t written = 0;

    while (written < size) {
        ssize_t result = write(descriptor, bytes + written, size - written);

        if (result > 0) {
            written += (size_t)result;
        } else if (result == -1 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int wc_expect_response(int descriptor, struct wc_buffer *input,
                              const void *expected, size_t expected_length)
{
    for (;;) {
        struct wc_response response;
        size_t consumed;
        enum wc_parse_result parse_result = wc_protocol_parse_response(
            input->data, input->length, &response, &consumed);

        if (parse_result == WC_PARSE_COMPLETE) {
            if (response.data_length != expected_length ||
                (expected_length > 0 &&
                 memcmp(response.data, expected, expected_length) != 0)) {
                errno = EPROTO;
                return -1;
            }
            return wc_buffer_consume(input, consumed);
        }
        if (parse_result == WC_PARSE_INVALID) {
            errno = EPROTO;
            return -1;
        }

        {
            struct pollfd poll_descriptor = {descriptor, POLLIN, 0};
            unsigned char bytes[4096];
            int poll_result = poll(&poll_descriptor, 1, 3000);
            ssize_t bytes_read;

            if (poll_result == -1 && errno == EINTR) {
                continue;
            }
            if (poll_result <= 0) {
                errno = poll_result == 0 ? ETIMEDOUT : errno;
                return -1;
            }
            bytes_read = read(descriptor, bytes, sizeof(bytes));
            if (bytes_read == -1 && errno == EINTR) {
                continue;
            }
            if (bytes_read <= 0) {
                errno = EPIPE;
                return -1;
            }
            if (wc_buffer_append(input, bytes, (size_t)bytes_read) == -1) {
                return -1;
            }
        }
    }
}

static int wc_make_pwd_request(struct wc_buffer *request)
{
    if (wc_buffer_init(request, WC_PROTOCOL_REQUEST_HEADER_SIZE) == -1) {
        return -1;
    }
    if (wc_protocol_encode_request(request, WC_REQUEST_PWD, NULL, 0) == -1) {
        wc_buffer_destroy(request);
        return -1;
    }
    return 0;
}

/* Count entries in one Linux /proc directory for the server process. */
static int wc_count_process_entries(pid_t process_id, const char *entry_name,
                                    size_t *count)
{
    char directory_path[64];
    DIR *directory;
    struct dirent *entry;

    if (snprintf(directory_path, sizeof(directory_path), "/proc/%ld/%s",
                 (long)process_id, entry_name) < 0) {
        return -1;
    }
    directory = opendir(directory_path);
    if (directory == NULL) {
        return -1;
    }

    *count = 0;
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            ++*count;
        }
    }
    if (errno != 0) {
        int saved_errno = errno;

        (void)closedir(directory);
        errno = saved_errno;
        return -1;
    }
    return closedir(directory);
}

static int wc_count_process_descriptors(pid_t process_id, size_t *count)
{
    return wc_count_process_entries(process_id, "fd", count);
}

static int test_uds_fragmented_request_returns_response(
    const char *server_program)
{
    struct wc_server_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer response;
    char working_directory[4096];

    WC_INTEGRATION_ASSERT(getcwd(working_directory,
                                 sizeof(working_directory)) != NULL);
    WC_INTEGRATION_ASSERT(wc_start_server(&fixture, server_program, 1) == 0);
    WC_INTEGRATION_ASSERT(wc_make_pwd_request(&request) == 0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&response, UINT16_MAX + 2U) == 0);

    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, request.data, 2) ==
                          0);
    wc_short_pause();
    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, request.data + 2,
                                       request.length - 2) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              fixture.client_fd, &response, working_directory,
                              strlen(working_directory)) == 0);

    wc_buffer_destroy(&request);
    wc_buffer_destroy(&response);
    WC_INTEGRATION_ASSERT(wc_stop_server(&fixture) == 0);
    return 0;
}

static int test_uds_coalesced_requests_return_two_responses(
    const char *server_program)
{
    struct wc_server_fixture fixture;
    struct wc_buffer requests;
    struct wc_buffer response;
    char working_directory[4096];

    WC_INTEGRATION_ASSERT(getcwd(working_directory,
                                 sizeof(working_directory)) != NULL);
    WC_INTEGRATION_ASSERT(wc_start_server(&fixture, server_program, 2) == 0);
    WC_INTEGRATION_ASSERT(
        wc_buffer_init(&requests, WC_PROTOCOL_REQUEST_HEADER_SIZE * 2U) == 0);
    WC_INTEGRATION_ASSERT(wc_protocol_encode_request(
                              &requests, WC_REQUEST_PWD, NULL, 0) == 0);
    WC_INTEGRATION_ASSERT(wc_protocol_encode_request(
                              &requests, WC_REQUEST_PWD, NULL, 0) == 0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&response, UINT16_MAX + 2U) == 0);

    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, requests.data,
                                       requests.length) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              fixture.client_fd, &response, working_directory,
                              strlen(working_directory)) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              fixture.client_fd, &response, working_directory,
                              strlen(working_directory)) == 0);

    wc_buffer_destroy(&requests);
    wc_buffer_destroy(&response);
    WC_INTEGRATION_ASSERT(wc_stop_server(&fixture) == 0);
    return 0;
}

static int test_uds_two_clients_receive_independent_responses(
    const char *server_program)
{
    struct wc_server_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer first_response;
    struct wc_buffer second_response;
    char working_directory[4096];
    int second_client;

    WC_INTEGRATION_ASSERT(getcwd(working_directory,
                                 sizeof(working_directory)) != NULL);
    WC_INTEGRATION_ASSERT(wc_start_server(&fixture, server_program, 3) == 0);
    second_client = wc_connect_to_server(fixture.socket_path);
    WC_INTEGRATION_ASSERT(second_client != -1);
    WC_INTEGRATION_ASSERT(wc_make_pwd_request(&request) == 0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&first_response, UINT16_MAX + 2U) ==
                          0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&second_response, UINT16_MAX + 2U) ==
                          0);

    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, request.data,
                                       request.length) == 0);
    WC_INTEGRATION_ASSERT(
        wc_write_all(second_client, request.data, request.length) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              fixture.client_fd, &first_response,
                              working_directory, strlen(working_directory)) ==
                          0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              second_client, &second_response,
                              working_directory, strlen(working_directory)) ==
                          0);

    WC_INTEGRATION_ASSERT(close(second_client) == 0);
    wc_buffer_destroy(&request);
    wc_buffer_destroy(&first_response);
    wc_buffer_destroy(&second_response);
    WC_INTEGRATION_ASSERT(wc_stop_server(&fixture) == 0);
    return 0;
}

static int test_uds_invalid_request_disconnects_only_that_client(
    const char *server_program)
{
    static const unsigned char invalid_request[] = {0, 6, 0, 99, 0, 0};
    struct wc_server_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer response;
    struct pollfd poll_descriptor;
    char working_directory[4096];
    int valid_client;
    unsigned char byte;

    WC_INTEGRATION_ASSERT(getcwd(working_directory,
                                 sizeof(working_directory)) != NULL);
    WC_INTEGRATION_ASSERT(wc_start_server(&fixture, server_program, 4) == 0);
    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, invalid_request,
                                       sizeof(invalid_request)) == 0);
    poll_descriptor.fd = fixture.client_fd;
    poll_descriptor.events = POLLIN;
    poll_descriptor.revents = 0;
    WC_INTEGRATION_ASSERT(poll(&poll_descriptor, 1, 3000) > 0);
    WC_INTEGRATION_ASSERT(read(fixture.client_fd, &byte, 1) == 0);

    valid_client = wc_connect_to_server(fixture.socket_path);
    WC_INTEGRATION_ASSERT(valid_client != -1);
    WC_INTEGRATION_ASSERT(wc_make_pwd_request(&request) == 0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&response, UINT16_MAX + 2U) == 0);
    WC_INTEGRATION_ASSERT(
        wc_write_all(valid_client, request.data, request.length) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              valid_client, &response, working_directory,
                              strlen(working_directory)) == 0);

    WC_INTEGRATION_ASSERT(close(valid_client) == 0);
    wc_buffer_destroy(&request);
    wc_buffer_destroy(&response);
    WC_INTEGRATION_ASSERT(wc_stop_server(&fixture) == 0);
    return 0;
}

static int test_uds_disconnected_client_requests_are_discarded(
    const char *server_program)
{
    struct wc_server_fixture fixture;
    struct wc_buffer requests;
    struct wc_buffer response;
    char working_directory[4096];
    int surviving_client;
    int index;

    WC_INTEGRATION_ASSERT(getcwd(working_directory,
                                 sizeof(working_directory)) != NULL);
    WC_INTEGRATION_ASSERT(wc_start_server(&fixture, server_program, 5) == 0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&requests, 600) == 0);
    for (index = 0; index < 100; ++index) {
        WC_INTEGRATION_ASSERT(wc_protocol_encode_request(
                                  &requests, WC_REQUEST_PWD, NULL, 0) == 0);
    }
    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, requests.data,
                                       requests.length) == 0);
    WC_INTEGRATION_ASSERT(close(fixture.client_fd) == 0);
    fixture.client_fd = -1;

    surviving_client = wc_wait_for_connection(fixture.socket_path);
    WC_INTEGRATION_ASSERT(surviving_client != -1);
    requests.length = 0;
    WC_INTEGRATION_ASSERT(wc_protocol_encode_request(
                              &requests, WC_REQUEST_PWD, NULL, 0) == 0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&response, UINT16_MAX + 2U) == 0);
    WC_INTEGRATION_ASSERT(wc_write_all(surviving_client, requests.data,
                                       requests.length) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              surviving_client, &response, working_directory,
                              strlen(working_directory)) == 0);

    WC_INTEGRATION_ASSERT(close(surviving_client) == 0);
    wc_buffer_destroy(&requests);
    wc_buffer_destroy(&response);
    WC_INTEGRATION_ASSERT(wc_stop_server(&fixture) == 0);
    return 0;
}

static int test_uds_repeated_connections_release_descriptors(
    const char *server_program)
{
    struct wc_server_fixture fixture;
    struct wc_buffer request;
    struct wc_buffer response;
    char working_directory[4096];
    size_t descriptors_before;
    size_t descriptors_after;
    int index;

    WC_INTEGRATION_ASSERT(getcwd(working_directory,
                                 sizeof(working_directory)) != NULL);
    WC_INTEGRATION_ASSERT(wc_start_server(&fixture, server_program, 6) == 0);
    WC_INTEGRATION_ASSERT(wc_make_pwd_request(&request) == 0);
    WC_INTEGRATION_ASSERT(wc_buffer_init(&response, UINT16_MAX + 2U) == 0);

    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, request.data,
                                       request.length) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              fixture.client_fd, &response, working_directory,
                              strlen(working_directory)) == 0);
    WC_INTEGRATION_ASSERT(wc_count_process_descriptors(
                              fixture.process_id, &descriptors_before) == 0);

    for (index = 0; index < 50; ++index) {
        int short_client = wc_connect_to_server(fixture.socket_path);

        WC_INTEGRATION_ASSERT(short_client != -1);
        WC_INTEGRATION_ASSERT(close(short_client) == 0);
        wc_short_pause();
    }

    WC_INTEGRATION_ASSERT(wc_write_all(fixture.client_fd, request.data,
                                       request.length) == 0);
    WC_INTEGRATION_ASSERT(wc_expect_response(
                              fixture.client_fd, &response, working_directory,
                              strlen(working_directory)) == 0);
    WC_INTEGRATION_ASSERT(wc_count_process_descriptors(
                              fixture.process_id, &descriptors_after) == 0);
    WC_INTEGRATION_ASSERT(descriptors_after == descriptors_before);

    wc_buffer_destroy(&request);
    wc_buffer_destroy(&response);
    WC_INTEGRATION_ASSERT(wc_stop_server(&fixture) == 0);
    return 0;
}

static int test_uds_threaded_creates_one_worker_per_client(
    const char *server_program)
{
    struct wc_server_fixture fixture;
    int extra_clients[3];
    size_t thread_count = 0;
    int attempt;
    int index;

    WC_INTEGRATION_ASSERT(wc_start_server(&fixture, server_program, 7) == 0);
    for (index = 0; index < 3; ++index) {
        extra_clients[index] = wc_connect_to_server(fixture.socket_path);
        WC_INTEGRATION_ASSERT(extra_clients[index] != -1);
    }

    for (attempt = 0; attempt < 100; ++attempt) {
        WC_INTEGRATION_ASSERT(wc_count_process_entries(
                                  fixture.process_id, "task",
                                  &thread_count) == 0);
        if (thread_count >= 5) {
            break;
        }
        wc_short_pause();
    }
    WC_INTEGRATION_ASSERT(thread_count >= 5);

    for (index = 0; index < 3; ++index) {
        WC_INTEGRATION_ASSERT(close(extra_clients[index]) == 0);
    }
    WC_INTEGRATION_ASSERT(wc_stop_server(&fixture) == 0);
    return 0;
}

int main(int argument_count, char **arguments)
{
    struct wc_integration_test {
        const char *name;
        int (*function)(const char *server_program);
    };
    static const struct wc_integration_test tests[] = {
        {"test_uds_fragmented_request_returns_response",
         test_uds_fragmented_request_returns_response},
        {"test_uds_coalesced_requests_return_two_responses",
         test_uds_coalesced_requests_return_two_responses},
        {"test_uds_two_clients_receive_independent_responses",
         test_uds_two_clients_receive_independent_responses},
        {"test_uds_invalid_request_disconnects_only_that_client",
         test_uds_invalid_request_disconnects_only_that_client},
        {"test_uds_disconnected_client_requests_are_discarded",
         test_uds_disconnected_client_requests_are_discarded},
        {"test_uds_repeated_connections_release_descriptors",
         test_uds_repeated_connections_release_descriptors},
        {"test_uds_threaded_creates_one_worker_per_client",
         test_uds_threaded_creates_one_worker_per_client},
    };
    size_t index;
    size_t failures = 0;
    struct sigaction timeout_action = {0};

    if (argument_count != 2 && argument_count != 3) {
        fprintf(stderr, "Usage: %s SERVER_PROGRAM [threaded]\n", arguments[0]);
        return 2;
    }

    timeout_action.sa_handler = wc_handle_test_timeout;
    WC_INTEGRATION_ASSERT(sigemptyset(&timeout_action.sa_mask) == 0);
    WC_INTEGRATION_ASSERT(sigaction(SIGALRM, &timeout_action, NULL) == 0);
    alarm(30);
    for (index = 0;
         index < sizeof(tests) / sizeof(tests[0]) -
                     (argument_count == 3 ? 0U : 1U);
         ++index) {
        int result = tests[index].function(arguments[1]);

        wc_force_stop_active_server();

        if (result == 0) {
            printf("ok %zu - %s\n", index + 1, tests[index].name);
        } else {
            printf("not ok %zu - %s\n", index + 1, tests[index].name);
            ++failures;
        }
    }
    printf("1..%zu\n", sizeof(tests) / sizeof(tests[0]) -
                            (argument_count == 3 ? 0U : 1U));
    return failures == 0 ? 0 : 1;
}
