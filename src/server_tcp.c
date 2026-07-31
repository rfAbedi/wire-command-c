#define _POSIX_C_SOURCE 200809L

#include "wirecommand/server_tcp.h"

#include "wirecommand/buffer.h"
#include "wirecommand/commands.h"
#include "wirecommand/logging.h"
#include "wirecommand/protocol.h"
#include "wirecommand/socket_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum {
    WC_TCP_MAX_CLIENTS = 64,
    WC_TCP_POLL_TIMEOUT_MS = 250,
    WC_TCP_READ_SIZE = 4096,
    WC_TCP_INPUT_LIMIT = UINT16_MAX,
    WC_TCP_OUTPUT_LIMIT = UINT16_MAX + WC_PROTOCOL_RESPONSE_HEADER_SIZE
};

struct wc_tcp_server;

struct wc_tcp_worker {
    pthread_t thread;
    int client_fd;
    int in_use;
    int finished;
    struct wc_tcp_server *server;
};

struct wc_tcp_server {
    pthread_mutex_t workers_lock;
    struct wc_tcp_worker workers[WC_TCP_MAX_CLIENTS];
    int stopping;
};

/* Read the shared shutdown state without racing the main thread. */
static int wc_tcp_is_stopping(struct wc_tcp_server *server)
{
    int stopping;

    if (pthread_mutex_lock(&server->workers_lock) != 0) {
        return 1;
    }
    stopping = server->stopping;
    if (pthread_mutex_unlock(&server->workers_lock) != 0) {
        return 1;
    }
    return stopping;
}

/* Publish shutdown after the signal handler returns to normal control flow. */
static int wc_tcp_request_stop(struct wc_tcp_server *server)
{
    int lock_result = pthread_mutex_lock(&server->workers_lock);
    int unlock_result;

    if (lock_result != 0) {
        return lock_result;
    }
    server->stopping = 1;
    unlock_result = pthread_mutex_unlock(&server->workers_lock);
    return unlock_result;
}

static int wc_tcp_open_listener(const char *bind_address, uint16_t port)
{
    struct sockaddr_in address = {0};
    int listener_fd;
    int reuse_address = 1;
    int address_result;

    if (bind_address == NULL || bind_address[0] == '\0' || port == 0) {
        errno = EINVAL;
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address_result = inet_pton(AF_INET, bind_address, &address.sin_addr);
    if (address_result != 1) {
        if (address_result == 0) {
            errno = EINVAL;
        }
        return -1;
    }

    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd == -1) {
        return -1;
    }
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) == -1 ||
        wc_socket_set_nonblocking(listener_fd) == -1 ||
        bind(listener_fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(listener_fd, WC_TCP_MAX_CLIENTS) == -1) {
        int saved_errno = errno;

        (void)close(listener_fd);
        errno = saved_errno;
        return -1;
    }
    return listener_fd;
}

static int wc_tcp_run_command(enum wc_request_type type,
                              const unsigned char *argument,
                              size_t argument_length,
                              struct wc_command_result *result)
{
    switch (type) {
    case WC_REQUEST_LS:
        return wc_command_ls(argument, argument_length, UINT16_MAX, result);
    case WC_REQUEST_PWD:
        if (argument_length != 0) {
            errno = EINVAL;
            return -1;
        }
        return wc_command_pwd(UINT16_MAX, result);
    case WC_REQUEST_CAT:
        return wc_command_cat(argument, argument_length, UINT16_MAX, result);
    }

    errno = EINVAL;
    return -1;
}

static int wc_tcp_encode_response(struct wc_buffer *output,
                                  const struct wc_request *request,
                                  int client_fd)
{
    struct wc_command_result result = {0};

    if (wc_tcp_run_command(request->type, request->argument,
                           request->argument_length, &result) == 0) {
        int encode_result =
            wc_protocol_encode_response(output, result.data, result.length);

        wc_command_result_destroy(&result);
        return encode_result;
    }

    {
        char error_text[256];
        int command_errno = errno;
        int text_length;

        wc_log(WC_LOG_ERROR, "commands", "command_failed",
               "transport=tcp client=fd:%d thread=%lu type=%u errno=%d",
               client_fd, (unsigned long)pthread_self(),
               (unsigned int)request->type, command_errno);
        text_length = snprintf(error_text, sizeof(error_text), "ERROR: %s",
                               strerror(command_errno));
        if (text_length < 0) {
            errno = EIO;
            return -1;
        }
        if ((size_t)text_length >= sizeof(error_text)) {
            text_length = (int)sizeof(error_text) - 1;
        }
        return wc_protocol_encode_response(output, error_text,
                                           (size_t)text_length);
    }
}

/* Process one request after the previous response has drained. */
static int wc_tcp_process_request(struct wc_buffer *input,
                                  struct wc_buffer *output, int client_fd)
{
    struct wc_request request;
    size_t consumed;
    enum wc_parse_result parse_result = wc_protocol_parse_request(
        input->data, input->length, &request, &consumed);

    if (parse_result == WC_PARSE_NEED_MORE) {
        return 0;
    }
    if (parse_result == WC_PARSE_INVALID) {
        errno = EPROTO;
        wc_log(WC_LOG_WARN, "protocol", "request_rejected",
               "transport=tcp client=fd:%d thread=%lu "
               "reason=invalid_message",
               client_fd, (unsigned long)pthread_self());
        return -1;
    }
    if (wc_tcp_encode_response(output, &request, client_fd) == -1) {
        return -1;
    }
    wc_log(WC_LOG_DEBUG, "protocol", "request_complete",
           "transport=tcp client=fd:%d thread=%lu type=%u "
           "message_length=%zu",
           client_fd, (unsigned long)pthread_self(),
           (unsigned int)request.type, request.message_length);
    return wc_buffer_consume(input, consumed);
}

static int wc_tcp_read_client(int client_fd, struct wc_buffer *input)
{
    unsigned char bytes[WC_TCP_READ_SIZE];
    size_t available = input->max_size - input->length;
    size_t read_size = available < sizeof(bytes) ? available : sizeof(bytes);
    ssize_t bytes_read;

    if (read_size == 0) {
        errno = EMSGSIZE;
        return -1;
    }
    bytes_read = read(client_fd, bytes, read_size);
    if (bytes_read > 0) {
        return wc_buffer_append(input, bytes, (size_t)bytes_read);
    }
    if (bytes_read == 0) {
        errno = ECONNRESET;
        return -1;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
    }
    return -1;
}

static int wc_tcp_write_client(int client_fd, struct wc_buffer *output)
{
    ssize_t bytes_written = send(client_fd, output->data, output->length,
                                 MSG_NOSIGNAL);

    if (bytes_written > 0) {
        return wc_buffer_consume(output, (size_t)bytes_written);
    }
    if (bytes_written == -1 &&
        (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return -1;
}

static void wc_tcp_mark_finished(struct wc_tcp_worker *worker)
{
    int lock_result = pthread_mutex_lock(&worker->server->workers_lock);

    if (lock_result != 0) {
        wc_log(WC_LOG_ERROR, "server", "worker_lock_failed",
               "transport=tcp thread=%lu error=%d",
               (unsigned long)pthread_self(), lock_result);
        return;
    }
    worker->finished = 1;
    lock_result = pthread_mutex_unlock(&worker->server->workers_lock);
    if (lock_result != 0) {
        wc_log(WC_LOG_ERROR, "server", "worker_unlock_failed",
               "transport=tcp thread=%lu error=%d",
               (unsigned long)pthread_self(), lock_result);
    }
}

static void *wc_tcp_worker_main(void *argument)
{
    struct wc_tcp_worker *worker = argument;
    struct wc_tcp_server *server = worker->server;
    struct wc_buffer input;
    struct wc_buffer output;
    int client_fd = worker->client_fd;
    int input_ready = 0;
    int output_ready = 0;

    if (wc_buffer_init(&input, WC_TCP_INPUT_LIMIT) == -1) {
        wc_log(WC_LOG_ERROR, "server", "worker_buffer_failed",
               "transport=tcp client=fd:%d thread=%lu buffer=input "
               "errno=%d",
               client_fd, (unsigned long)pthread_self(), errno);
        (void)close(client_fd);
        wc_tcp_mark_finished(worker);
        return NULL;
    }
    input_ready = 1;
    if (wc_buffer_init(&output, WC_TCP_OUTPUT_LIMIT) == -1) {
        wc_log(WC_LOG_ERROR, "server", "worker_buffer_failed",
               "transport=tcp client=fd:%d thread=%lu buffer=output "
               "errno=%d",
               client_fd, (unsigned long)pthread_self(), errno);
        wc_buffer_destroy(&input);
        (void)close(client_fd);
        wc_tcp_mark_finished(worker);
        return NULL;
    }
    output_ready = 1;

    wc_log(WC_LOG_INFO, "server", "worker_start",
           "transport=tcp client=fd:%d thread=%lu", client_fd,
           (unsigned long)pthread_self());

    while (!wc_tcp_is_stopping(server)) {
        struct pollfd descriptor = {client_fd, 0, 0};
        int poll_result;

        if (output.length == 0 &&
            wc_tcp_process_request(&input, &output, client_fd) == -1) {
            break;
        }
        descriptor.events = output.length > 0 ? POLLOUT : POLLIN;
        poll_result = poll(&descriptor, 1, WC_TCP_POLL_TIMEOUT_MS);
        if (poll_result == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (poll_result == 0) {
            continue;
        }
        if ((descriptor.revents & POLLIN) != 0 &&
            wc_tcp_read_client(client_fd, &input) == -1) {
            break;
        }
        if ((descriptor.revents & POLLOUT) != 0 &&
            wc_tcp_write_client(client_fd, &output) == -1) {
            break;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            break;
        }
        if ((descriptor.revents & POLLHUP) != 0 && output.length == 0) {
            break;
        }
    }

    if (input_ready) {
        wc_buffer_destroy(&input);
    }
    if (output_ready) {
        wc_buffer_destroy(&output);
    }
    if (close(client_fd) == -1) {
        wc_log(WC_LOG_WARN, "server", "close_failed",
               "transport=tcp client=fd:%d thread=%lu errno=%d", client_fd,
               (unsigned long)pthread_self(), errno);
    }
    wc_log(WC_LOG_INFO, "server", "worker_stop",
           "transport=tcp client=fd:%d thread=%lu", client_fd,
           (unsigned long)pthread_self());
    wc_tcp_mark_finished(worker);
    return NULL;
}

static void wc_tcp_workers_init(struct wc_tcp_server *server)
{
    size_t index;

    for (index = 0; index < WC_TCP_MAX_CLIENTS; ++index) {
        server->workers[index].client_fd = -1;
        server->workers[index].in_use = 0;
        server->workers[index].finished = 0;
        server->workers[index].server = server;
    }
}

/* Join completed workers before their fixed slots are reused. */
static void wc_tcp_reap_finished(struct wc_tcp_server *server)
{
    size_t index;

    for (index = 0; index < WC_TCP_MAX_CLIENTS; ++index) {
        pthread_t thread;
        int should_join = 0;
        int lock_result = pthread_mutex_lock(&server->workers_lock);

        if (lock_result != 0) {
            wc_log(WC_LOG_ERROR, "server", "worker_lock_failed",
                   "transport=tcp error=%d", lock_result);
            return;
        }
        if (server->workers[index].in_use &&
            server->workers[index].finished) {
            thread = server->workers[index].thread;
            should_join = 1;
        }
        lock_result = pthread_mutex_unlock(&server->workers_lock);
        if (lock_result != 0) {
            wc_log(WC_LOG_ERROR, "server", "worker_unlock_failed",
                   "transport=tcp error=%d", lock_result);
            return;
        }

        if (should_join) {
            int join_result = pthread_join(thread, NULL);

            if (join_result != 0) {
                wc_log(WC_LOG_ERROR, "server", "worker_join_failed",
                       "transport=tcp error=%d", join_result);
                continue;
            }
            server->workers[index].client_fd = -1;
            server->workers[index].in_use = 0;
        }
    }
}

static int wc_tcp_start_worker(struct wc_tcp_server *server, int client_fd)
{
    size_t index;

    for (index = 0; index < WC_TCP_MAX_CLIENTS; ++index) {
        struct wc_tcp_worker *worker = &server->workers[index];
        int create_result;
        int mask_result;
        int restore_result;
        sigset_t stop_signals;
        sigset_t previous_signals;

        if (worker->in_use) {
            continue;
        }
        worker->client_fd = client_fd;
        worker->finished = 0;
        worker->in_use = 1;

        if (sigemptyset(&stop_signals) == -1 ||
            sigaddset(&stop_signals, SIGINT) == -1 ||
            sigaddset(&stop_signals, SIGTERM) == -1) {
            worker->client_fd = -1;
            worker->in_use = 0;
            return -1;
        }
        mask_result = pthread_sigmask(SIG_BLOCK, &stop_signals,
                                      &previous_signals);
        if (mask_result != 0) {
            worker->client_fd = -1;
            worker->in_use = 0;
            errno = mask_result;
            return -1;
        }
        create_result = pthread_create(&worker->thread, NULL,
                                       wc_tcp_worker_main, worker);
        restore_result = pthread_sigmask(SIG_SETMASK, &previous_signals, NULL);
        if (create_result != 0) {
            worker->client_fd = -1;
            worker->in_use = 0;
            errno = create_result;
            return -1;
        }
        if (restore_result != 0) {
            wc_log(WC_LOG_ERROR, "server", "signal_mask_restore_failed",
                   "transport=tcp error=%d", restore_result);
        }
        return 0;
    }

    errno = EMFILE;
    return -1;
}

static void wc_tcp_accept_clients(int listener_fd,
                                  struct wc_tcp_server *server)
{
    for (;;) {
        int client_fd = accept(listener_fd, NULL, NULL);

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                wc_log(WC_LOG_ERROR, "server", "accept_failed",
                       "transport=tcp errno=%d", errno);
            }
            return;
        }

        wc_tcp_reap_finished(server);
        if (wc_socket_set_nonblocking(client_fd) == -1 ||
            wc_tcp_start_worker(server, client_fd) == -1) {
            int saved_errno = errno;

            (void)close(client_fd);
            wc_log(WC_LOG_WARN, "server", "client_rejected",
                   "transport=tcp errno=%d", saved_errno);
        }
    }
}

static int wc_tcp_join_all(struct wc_tcp_server *server)
{
    size_t index;
    int result = 0;

    for (index = 0; index < WC_TCP_MAX_CLIENTS; ++index) {
        if (server->workers[index].in_use) {
            int join_result = pthread_join(server->workers[index].thread,
                                           NULL);

            if (join_result != 0) {
                wc_log(WC_LOG_ERROR, "server", "worker_join_failed",
                       "transport=tcp error=%d", join_result);
                result = -1;
            } else {
                server->workers[index].client_fd = -1;
                server->workers[index].in_use = 0;
            }
        }
    }
    return result;
}

int wc_server_tcp_run(const char *bind_address, uint16_t port,
                      const volatile sig_atomic_t *stop_requested)
{
    struct wc_tcp_server server = {0};
    int listener_fd;
    int result = 0;
    int mutex_result;

    if (stop_requested == NULL) {
        errno = EINVAL;
        return -1;
    }
    mutex_result = pthread_mutex_init(&server.workers_lock, NULL);
    if (mutex_result != 0) {
        errno = mutex_result;
        return -1;
    }
    wc_tcp_workers_init(&server);

    listener_fd = wc_tcp_open_listener(bind_address, port);
    if (listener_fd == -1) {
        (void)pthread_mutex_destroy(&server.workers_lock);
        return -1;
    }
    wc_log(WC_LOG_INFO, "server", "start",
           "transport=tcp bind=%s port=%u", bind_address,
           (unsigned int)port);

    while (!*stop_requested) {
        struct pollfd listener = {listener_fd, POLLIN, 0};
        int poll_result;

        wc_tcp_reap_finished(&server);
        poll_result = poll(&listener, 1, WC_TCP_POLL_TIMEOUT_MS);
        if (poll_result == -1) {
            if (errno == EINTR) {
                continue;
            }
            result = -1;
            break;
        }
        if ((listener.revents & POLLIN) != 0) {
            wc_tcp_accept_clients(listener_fd, &server);
        }
        if ((listener.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            errno = EIO;
            result = -1;
            break;
        }
    }

    {
        int saved_errno = errno;
        int workers_joined;

        mutex_result = wc_tcp_request_stop(&server);
        if (mutex_result != 0 && result == 0) {
            saved_errno = mutex_result;
            result = -1;
        }
        if (close(listener_fd) == -1 && result == 0) {
            saved_errno = errno;
            result = -1;
        }
        workers_joined = wc_tcp_join_all(&server);
        if (workers_joined == -1 && result == 0) {
            saved_errno = EIO;
            result = -1;
        }
        if (workers_joined == 0) {
            mutex_result = pthread_mutex_destroy(&server.workers_lock);
            if (mutex_result != 0 && result == 0) {
                saved_errno = mutex_result;
                result = -1;
            }
        }
        wc_log(WC_LOG_INFO, "server", "shutdown", "transport=tcp");
        errno = saved_errno;
    }
    return result;
}
