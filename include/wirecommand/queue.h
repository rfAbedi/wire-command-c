#ifndef WIRECOMMAND_QUEUE_H
#define WIRECOMMAND_QUEUE_H

#include "wirecommand/protocol.h"

#include <stddef.h>

/*
 * A queued request owns its copied argument. next is used only by the queue.
 * After dequeue, the caller owns the request and must destroy it.
 */
struct wc_queued_request {
    int client_fd;
    enum wc_request_type type;
    size_t argument_length;
    struct wc_queued_request *next;
    unsigned char argument[];
};

struct wc_request_queue {
    struct wc_queued_request *head;
    struct wc_queued_request *tail;
    size_t length;
};

int wc_request_queue_init(struct wc_request_queue *queue);

/* Copy one parsed request and append it at the tail. */
int wc_request_queue_enqueue(struct wc_request_queue *queue, int client_fd,
                             enum wc_request_type type,
                             const unsigned char *argument,
                             size_t argument_length);

/* Remove the head request and transfer ownership to the caller. */
int wc_request_queue_dequeue(struct wc_request_queue *queue,
                             struct wc_queued_request **request);

/* Borrow the head request without removing it. Returns NULL when empty. */
const struct wc_queued_request *
wc_request_queue_peek(const struct wc_request_queue *queue);

/* Remove and destroy all requests belonging to client_fd. */
size_t wc_request_queue_discard_client(struct wc_request_queue *queue,
                                       int client_fd);

void wc_request_queue_clear(struct wc_request_queue *queue);
void wc_queued_request_destroy(struct wc_queued_request *request);

#endif
