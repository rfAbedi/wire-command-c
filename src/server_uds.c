#define _POSIX_C_SOURCE 200809L

#include "wirecommand/server_uds.h"

#include "wirecommand/buffer.h"
#include "wirecommand/commands.h"
#include "wirecommand/logging.h"
#include "wirecommand/protocol.h"
#include "wirecommand/queue.h"
#include "wirecommand/socket_utils.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum {
    WC_UDS_MAX_CLIENTS = 64,
    WC_UDS_MAX_QUEUED_REQUESTS = 256,
    WC_UDS_READ_SIZE = 4096,
    WC_UDS_INPUT_LIMIT = UINT16_MAX,
    WC_UDS_OUTPUT_LIMIT = UINT16_MAX + WC_PROTOCOL_RESPONSE_HEADER_SIZE
};

struct wc_uds_client {
    int fd;
    struct wc_buffer input;
    struct wc_buffer output;
};

static void wc_uds_clients_init(struct wc_uds_client *clients)
{
    size_t index;

    for (index = 0; index < WC_UDS_MAX_CLIENTS; ++index) {
        clients[index].fd = -1;
    }
    wc_log(WC_LOG_DEBUG, "server", "client_table_initialized",
           "transport=uds slots=%d", WC_UDS_MAX_CLIENTS);
}

static struct wc_uds_client *wc_uds_find_client(struct wc_uds_client *clients,
                                                int client_fd)
{
    size_t index;

    wc_log(WC_LOG_TRACE, "server", "client_lookup",
           "transport=uds client=fd:%d", client_fd);

    for (index = 0; index < WC_UDS_MAX_CLIENTS; ++index) {
        if (clients[index].fd == client_fd) {
            return &clients[index];
        }
    }
    return NULL;
}

static int wc_uds_has_free_client_slot(const struct wc_uds_client *clients)
{
    size_t index;

    for (index = 0; index < WC_UDS_MAX_CLIENTS; ++index) {
        if (clients[index].fd == -1) {
            return 1;
        }
    }
    return 0;
}

/* Add a client only after both of its owned buffers are initialized. */
static int wc_uds_add_client(struct wc_uds_client *clients, int client_fd)
{
    size_t index;

    for (index = 0; index < WC_UDS_MAX_CLIENTS; ++index) {
        if (clients[index].fd != -1) {
            continue;
        }
        if (wc_buffer_init(&clients[index].input, WC_UDS_INPUT_LIMIT) == -1) {
            return -1;
        }
        if (wc_buffer_init(&clients[index].output, WC_UDS_OUTPUT_LIMIT) == -1) {
            wc_buffer_destroy(&clients[index].input);
            return -1;
        }
        clients[index].fd = client_fd;
        wc_log(WC_LOG_DEBUG, "server", "client_added",
               "transport=uds client=fd:%d slot=%zu", client_fd, index);
        return 0;
    }

    errno = EMFILE;
    return -1;
}

static void wc_uds_remove_client(struct wc_uds_client *client,
                                 struct wc_request_queue *queue)
{
    int client_fd;

    if (client == NULL || client->fd == -1) {
        return;
    }

    client_fd = client->fd;
    (void)wc_request_queue_discard_client(queue, client_fd);
    wc_buffer_destroy(&client->input);
    wc_buffer_destroy(&client->output);
    client->fd = -1;

    if (close(client_fd) == -1) {
        wc_log(WC_LOG_WARN, "server", "close_failed", "client=fd:%d errno=%d",
               client_fd, errno);
    }
    wc_log(WC_LOG_INFO, "server", "disconnect", "client=fd:%d", client_fd);
}

static int wc_uds_open_listener(const char *socket_path)
{
    struct sockaddr_un address = {0};
    int listener_fd;

    if (socket_path == NULL || socket_path[0] == '\0' ||
        strlen(socket_path) >= sizeof(address.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    listener_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener_fd == -1) {
        return -1;
    }
    if (wc_socket_set_nonblocking(listener_fd) == -1) {
        int saved_errno = errno;

        (void)close(listener_fd);
        errno = saved_errno;
        return -1;
    }

    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1);
    if (unlink(socket_path) == -1 && errno != ENOENT) {
        int saved_errno = errno;

        (void)close(listener_fd);
        errno = saved_errno;
        return -1;
    }
    if (bind(listener_fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(listener_fd, WC_UDS_MAX_CLIENTS) == -1) {
        int saved_errno = errno;

        (void)close(listener_fd);
        (void)unlink(socket_path);
        errno = saved_errno;
        return -1;
    }
    wc_log(WC_LOG_DEBUG, "server", "listener_ready",
           "transport=uds listener=fd:%d socket=%s", listener_fd,
           socket_path);
    return listener_fd;
}

/* Accept every connection already waiting, then return to poll(). */
static void wc_uds_accept_clients(int listener_fd,
                                  struct wc_uds_client *clients)
{
    for (;;) {
        int client_fd;

        /* Leave excess connections in the listen backlog until a slot opens. */
        if (!wc_uds_has_free_client_slot(clients)) {
            return;
        }
        client_fd = accept(listener_fd, NULL, NULL);

        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                wc_log(WC_LOG_ERROR, "server", "accept_failed", "errno=%d",
                       errno);
            }
            return;
        }

        if (wc_socket_set_nonblocking(client_fd) == -1 ||
            wc_uds_add_client(clients, client_fd) == -1) {
            int saved_errno = errno;

            (void)close(client_fd);
            wc_log(WC_LOG_WARN, "server", "client_rejected", "errno=%d",
                   saved_errno);
            continue;
        }
        wc_log(WC_LOG_INFO, "server", "connect", "client=fd:%d", client_fd);
    }
}

/* Parse all complete frames now so the input buffer stays small. */
static int wc_uds_enqueue_complete_requests(
    struct wc_uds_client *client, struct wc_request_queue *queue)
{
    for (;;) {
        struct wc_request request;
        size_t consumed;
        enum wc_parse_result parse_result = wc_protocol_parse_request(
            client->input.data, client->input.length, &request, &consumed);

        if (parse_result == WC_PARSE_NEED_MORE) {
            return 0;
        }
        if (parse_result == WC_PARSE_INVALID) {
            errno = EPROTO;
            wc_log(WC_LOG_WARN, "protocol", "request_rejected",
                   "client=fd:%d reason=invalid_message", client->fd);
            return -1;
        }

        if (queue->length >= WC_UDS_MAX_QUEUED_REQUESTS) {
            errno = ENOBUFS;
            wc_log(WC_LOG_WARN, "server", "request_queue_full",
                   "client=fd:%d limit=%d", client->fd,
                   WC_UDS_MAX_QUEUED_REQUESTS);
            return -1;
        }
        if (wc_request_queue_enqueue(queue, client->fd, request.type,
                                     request.argument,
                                     request.argument_length) == -1) {
            return -1;
        }
        wc_log(WC_LOG_DEBUG, "protocol", "request_complete",
               "client=fd:%d type=%u message_length=%zu", client->fd,
               (unsigned int)request.type, request.message_length);
        if (wc_buffer_consume(&client->input, consumed) == -1) {
            return -1;
        }
    }
}

static int wc_uds_read_client(struct wc_uds_client *client,
                              struct wc_request_queue *queue)
{
    for (;;) {
        unsigned char bytes[WC_UDS_READ_SIZE];
        size_t available = client->input.max_size - client->input.length;
        size_t read_size = available < sizeof(bytes) ? available : sizeof(bytes);
        ssize_t bytes_read;

        if (read_size == 0) {
            errno = EMSGSIZE;
            return -1;
        }
        bytes_read = read(client->fd, bytes, read_size);

        if (bytes_read > 0) {
            if (wc_buffer_append(&client->input, bytes,
                                 (size_t)bytes_read) == -1 ||
                wc_uds_enqueue_complete_requests(client, queue) == -1) {
                return -1;
            }
            wc_log(WC_LOG_DEBUG, "server", "read_complete",
                   "client=fd:%d bytes=%zu input_pending=%zu",
                   client->fd, (size_t)bytes_read, client->input.length);
            continue;
        }
        if (bytes_read == 0) {
            return -1;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
}

static int wc_uds_write_client(struct wc_uds_client *client)
{
    while (client->output.length > 0) {
        ssize_t bytes_written = send(client->fd, client->output.data,
                                     client->output.length, MSG_NOSIGNAL);

        if (bytes_written > 0) {
            if (wc_buffer_consume(&client->output,
                                  (size_t)bytes_written) == -1) {
                return -1;
            }
            wc_log(WC_LOG_DEBUG, "server", "write_complete",
                   "client=fd:%d bytes=%zu output_pending=%zu",
                   client->fd, (size_t)bytes_written,
                   client->output.length);
            continue;
        }
        if (bytes_written == -1 && errno == EINTR) {
            continue;
        }
        if (bytes_written == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        return -1;
    }
    return 0;
}

static int wc_uds_run_command(const struct wc_queued_request *request,
                              struct wc_command_result *result)
{
    wc_log(WC_LOG_DEBUG, "commands", "command_dispatch",
           "transport=uds client=fd:%d type=%u argument_length=%zu",
           request->client_fd, (unsigned int)request->type,
           request->argument_length);

    switch (request->type) {
    case WC_REQUEST_LS:
        return wc_command_ls(request->argument, request->argument_length,
                             UINT16_MAX, result);
    case WC_REQUEST_PWD:
        if (request->argument_length != 0) {
            errno = EINVAL;
            return -1;
        }
        return wc_command_pwd(UINT16_MAX, result);
    case WC_REQUEST_CAT:
        return wc_command_cat(request->argument, request->argument_length,
                              UINT16_MAX, result);
    }

    errno = EINVAL;
    return -1;
}

/* The response format has no status field, so errors are ordinary text data. */
static int wc_uds_encode_command_response(
    struct wc_uds_client *client, const struct wc_queued_request *request)
{
    struct wc_command_result result = {0};

    if (wc_uds_run_command(request, &result) == 0) {
        int encode_result = wc_protocol_encode_response(
            &client->output, result.data, result.length);

        wc_command_result_destroy(&result);
        return encode_result;
    } else {
        char error_text[256];
        int command_errno = errno;
        int text_length;

        wc_log(WC_LOG_ERROR, "commands", "command_failed",
               "client=fd:%d type=%u errno=%d", client->fd,
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
        return wc_protocol_encode_response(&client->output, error_text,
                                           (size_t)text_length);
    }
}

/* Process FIFO entries until the next client's prior response must drain. */
static void wc_uds_process_queue(struct wc_uds_client *clients,
                                 struct wc_request_queue *queue)
{
    for (;;) {
        const struct wc_queued_request *first = wc_request_queue_peek(queue);
        struct wc_queued_request *request;
        struct wc_uds_client *client;

        if (first == NULL) {
            return;
        }
        client = wc_uds_find_client(clients, first->client_fd);
        if (client == NULL) {
            if (wc_request_queue_dequeue(queue, &request) == 0) {
                wc_queued_request_destroy(request);
            }
            continue;
        }
        if (client->output.length != 0) {
            return;
        }
        if (wc_request_queue_dequeue(queue, &request) == -1) {
            return;
        }

        if (wc_uds_encode_command_response(client, request) == -1) {
            wc_log(WC_LOG_ERROR, "server", "response_failed",
                   "client=fd:%d errno=%d", client->fd, errno);
            wc_queued_request_destroy(request);
            wc_uds_remove_client(client, queue);
            continue;
        }
        wc_log(WC_LOG_DEBUG, "server", "response_queued",
               "client=fd:%d type=%u output_pending=%zu", client->fd,
               (unsigned int)request->type, client->output.length);
        wc_queued_request_destroy(request);
    }
}

static nfds_t wc_uds_build_poll_list(struct pollfd *poll_descriptors,
                                     size_t *client_indexes, int listener_fd,
                                     struct wc_uds_client *clients)
{
    nfds_t count = 1;
    size_t index;

    poll_descriptors[0].fd = listener_fd;
    /* At capacity, wait for a client slot instead of accepting and rejecting. */
    poll_descriptors[0].events =
        wc_uds_has_free_client_slot(clients) ? POLLIN : 0;
    poll_descriptors[0].revents = 0;

    for (index = 0; index < WC_UDS_MAX_CLIENTS; ++index) {
        if (clients[index].fd == -1) {
            continue;
        }
        poll_descriptors[count].fd = clients[index].fd;
        poll_descriptors[count].events = POLLIN;
        if (clients[index].output.length > 0) {
            poll_descriptors[count].events |= POLLOUT;
        }
        poll_descriptors[count].revents = 0;
        client_indexes[count] = index;
        ++count;
    }
    wc_log(WC_LOG_TRACE, "server", "poll_list_built",
           "transport=uds descriptors=%lu", (unsigned long)count);
    return count;
}

static void wc_uds_close_all_clients(struct wc_uds_client *clients,
                                     struct wc_request_queue *queue)
{
    size_t index;

    wc_log(WC_LOG_DEBUG, "server", "clients_close_all",
           "transport=uds");

    for (index = 0; index < WC_UDS_MAX_CLIENTS; ++index) {
        wc_uds_remove_client(&clients[index], queue);
    }
}

int wc_server_uds_run(const char *socket_path,
                      const volatile sig_atomic_t *stop_requested)
{
    struct wc_uds_client clients[WC_UDS_MAX_CLIENTS];
    struct wc_request_queue queue;
    struct pollfd poll_descriptors[WC_UDS_MAX_CLIENTS + 1];
    size_t client_indexes[WC_UDS_MAX_CLIENTS + 1];
    int listener_fd;
    int result = 0;

    if (stop_requested == NULL) {
        errno = EINVAL;
        return -1;
    }
    wc_uds_clients_init(clients);
    if (wc_request_queue_init(&queue) == -1) {
        return -1;
    }
    listener_fd = wc_uds_open_listener(socket_path);
    if (listener_fd == -1) {
        return -1;
    }

    wc_log(WC_LOG_INFO, "server", "start", "transport=uds socket=%s",
           socket_path);
    while (!*stop_requested) {
        nfds_t descriptor_count;
        int poll_result;
        nfds_t poll_index;

        wc_uds_process_queue(clients, &queue);
        descriptor_count = wc_uds_build_poll_list(
            poll_descriptors, client_indexes, listener_fd, clients);

        poll_result = poll(poll_descriptors, descriptor_count, -1);
        if (poll_result == -1) {
            if (errno == EINTR) {
                continue;
            }
            result = -1;
            break;
        }
        if ((poll_descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) !=
            0) {
            errno = EIO;
            result = -1;
            break;
        }

        for (poll_index = 1; poll_index < descriptor_count; ++poll_index) {
            struct wc_uds_client *client =
                &clients[client_indexes[poll_index]];
            short events = poll_descriptors[poll_index].revents;

            if (client->fd == -1) {
                continue;
            }
            if ((events & POLLIN) != 0 &&
                wc_uds_read_client(client, &queue) == -1) {
                wc_uds_remove_client(client, &queue);
                continue;
            }
            if ((events & POLLOUT) != 0 &&
                wc_uds_write_client(client) == -1) {
                wc_uds_remove_client(client, &queue);
                continue;
            }
            if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                wc_uds_remove_client(client, &queue);
            }
        }
        /* Reclaim closed client slots before accepting replacement clients. */
        if ((poll_descriptors[0].revents & POLLIN) != 0) {
            wc_uds_accept_clients(listener_fd, clients);
        }
    }

    {
        int saved_errno = errno;

        wc_uds_close_all_clients(clients, &queue);
        wc_request_queue_clear(&queue);
        if (close(listener_fd) == -1 && result == 0) {
            saved_errno = errno;
            result = -1;
        }
        if (unlink(socket_path) == -1 && errno != ENOENT && result == 0) {
            saved_errno = errno;
            result = -1;
        }
        wc_log(WC_LOG_INFO, "server", "shutdown", "transport=uds");
        errno = saved_errno;
    }
    return result;
}
