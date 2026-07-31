#include "wirecommand/queue.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int wc_request_type_valid(enum wc_request_type type)
{
    return type == WC_REQUEST_LS || type == WC_REQUEST_PWD ||
           type == WC_REQUEST_CAT;
}

int wc_request_queue_init(struct wc_request_queue *queue)
{
    if (queue == NULL) {
        errno = EINVAL;
        return -1;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->length = 0;
    return 0;
}

int wc_request_queue_enqueue(struct wc_request_queue *queue, int client_fd,
                             enum wc_request_type type,
                             const unsigned char *argument,
                             size_t argument_length)
{
    struct wc_queued_request *request;
    size_t allocation_size;

    if (queue == NULL || client_fd < 0 || !wc_request_type_valid(type) ||
        (argument == NULL && argument_length != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (argument_length > SIZE_MAX - sizeof(*request)) {
        errno = EOVERFLOW;
        return -1;
    }

    allocation_size = sizeof(*request) + argument_length;
    request = malloc(allocation_size);
    if (request == NULL) {
        return -1;
    }

    request->client_fd = client_fd;
    request->type = type;
    request->argument_length = argument_length;
    request->next = NULL;
    if (argument_length > 0) {
        memcpy(request->argument, argument, argument_length);
    }

    if (queue->tail == NULL) {
        queue->head = request;
    } else {
        queue->tail->next = request;
    }
    queue->tail = request;
    ++queue->length;
    return 0;
}

int wc_request_queue_dequeue(struct wc_request_queue *queue,
                             struct wc_queued_request **request)
{
    if (queue == NULL || request == NULL) {
        errno = EINVAL;
        return -1;
    }

    *request = NULL;
    if (queue->head == NULL) {
        errno = ENOENT;
        return -1;
    }

    *request = queue->head;
    queue->head = queue->head->next;
    (*request)->next = NULL;
    --queue->length;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    return 0;
}

size_t wc_request_queue_discard_client(struct wc_request_queue *queue,
                                       int client_fd)
{
    struct wc_queued_request *current;
    struct wc_queued_request *previous = NULL;
    size_t removed = 0;

    if (queue == NULL) {
        return 0;
    }

    current = queue->head;
    while (current != NULL) {
        if (current->client_fd == client_fd) {
            struct wc_queued_request *next = current->next;

            if (previous == NULL) {
                queue->head = next;
            } else {
                previous->next = next;
            }
            if (queue->tail == current) {
                queue->tail = previous;
            }

            free(current);
            current = next;
            --queue->length;
            ++removed;
        } else {
            previous = current;
            current = current->next;
        }
    }

    return removed;
}

void wc_request_queue_clear(struct wc_request_queue *queue)
{
    struct wc_queued_request *current;

    if (queue == NULL) {
        return;
    }

    current = queue->head;
    while (current != NULL) {
        struct wc_queued_request *next = current->next;

        free(current);
        current = next;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->length = 0;
}

void wc_queued_request_destroy(struct wc_queued_request *request)
{
    free(request);
}
