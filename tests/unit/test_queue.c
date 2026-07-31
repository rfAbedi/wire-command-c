#include "test.h"

#include "wirecommand/queue.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

int test_queue_initial_state_is_empty(void)
{
    struct wc_request_queue queue;

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);

    WC_TEST_ASSERT(queue.head == NULL);
    WC_TEST_ASSERT(queue.tail == NULL);
    WC_TEST_ASSERT(queue.length == 0);
    return 0;
}

int test_queue_empty_dequeue_returns_error(void)
{
    struct wc_request_queue queue;
    struct wc_queued_request *request = (struct wc_queued_request *)1;

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_request_queue_dequeue(&queue, &request) == -1);

    WC_TEST_ASSERT(errno == ENOENT);
    WC_TEST_ASSERT(request == NULL);
    return 0;
}

int test_queue_requests_are_dequeued_in_fifo_order(void)
{
    struct wc_request_queue queue;
    struct wc_queued_request *request;

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 10, WC_REQUEST_LS,
                       (const unsigned char *)"/a", 2) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 11, WC_REQUEST_PWD, NULL, 0) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 12, WC_REQUEST_CAT,
                       (const unsigned char *)"/c", 2) == 0);

    WC_TEST_ASSERT(wc_request_queue_dequeue(&queue, &request) == 0);
    WC_TEST_ASSERT(request->client_fd == 10);
    WC_TEST_ASSERT(request->type == WC_REQUEST_LS);
    wc_queued_request_destroy(request);

    WC_TEST_ASSERT(wc_request_queue_dequeue(&queue, &request) == 0);
    WC_TEST_ASSERT(request->client_fd == 11);
    WC_TEST_ASSERT(request->type == WC_REQUEST_PWD);
    wc_queued_request_destroy(request);

    WC_TEST_ASSERT(wc_request_queue_dequeue(&queue, &request) == 0);
    WC_TEST_ASSERT(request->client_fd == 12);
    WC_TEST_ASSERT(request->type == WC_REQUEST_CAT);
    wc_queued_request_destroy(request);

    WC_TEST_ASSERT(queue.length == 0);
    WC_TEST_ASSERT(queue.head == NULL);
    WC_TEST_ASSERT(queue.tail == NULL);
    return 0;
}

int test_queue_argument_is_copied(void)
{
    struct wc_request_queue queue;
    struct wc_queued_request *request;
    unsigned char argument[] = "/tmp";

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 5, WC_REQUEST_LS, argument, 4) == 0);
    argument[1] = 'X';

    WC_TEST_ASSERT(wc_request_queue_dequeue(&queue, &request) == 0);

    WC_TEST_ASSERT(request->argument_length == 4);
    WC_TEST_ASSERT(memcmp(request->argument, "/tmp", 4) == 0);

    wc_queued_request_destroy(request);
    return 0;
}

int test_queue_disconnected_client_requests_are_discarded(void)
{
    struct wc_request_queue queue;
    struct wc_queued_request *request;

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 7, WC_REQUEST_LS, NULL, 0) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 8, WC_REQUEST_PWD, NULL, 0) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 7, WC_REQUEST_CAT, NULL, 0) == 0);

    WC_TEST_ASSERT(wc_request_queue_discard_client(&queue, 7) == 2);

    WC_TEST_ASSERT(queue.length == 1);
    WC_TEST_ASSERT(wc_request_queue_dequeue(&queue, &request) == 0);
    WC_TEST_ASSERT(request->client_fd == 8);
    wc_queued_request_destroy(request);
    return 0;
}

int test_queue_remains_usable_after_client_discard(void)
{
    struct wc_request_queue queue;
    struct wc_queued_request *request;

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 1, WC_REQUEST_LS, NULL, 0) == 0);
    WC_TEST_ASSERT(wc_request_queue_discard_client(&queue, 1) == 1);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 2, WC_REQUEST_PWD, NULL, 0) == 0);

    WC_TEST_ASSERT(wc_request_queue_dequeue(&queue, &request) == 0);
    WC_TEST_ASSERT(request->client_fd == 2);
    wc_queued_request_destroy(request);
    return 0;
}

int test_queue_clear_removes_every_request(void)
{
    struct wc_request_queue queue;

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 1, WC_REQUEST_LS, NULL, 0) == 0);
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 2, WC_REQUEST_PWD, NULL, 0) == 0);

    wc_request_queue_clear(&queue);

    WC_TEST_ASSERT(queue.head == NULL);
    WC_TEST_ASSERT(queue.tail == NULL);
    WC_TEST_ASSERT(queue.length == 0);
    return 0;
}

int test_queue_allocation_failure_leaves_queue_empty(void)
{
    struct wc_request_queue queue;
    unsigned char byte = 1;
    size_t huge_length = SIZE_MAX - sizeof(struct wc_queued_request);

    WC_TEST_ASSERT(wc_request_queue_init(&queue) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_request_queue_enqueue(
                       &queue, 1, WC_REQUEST_CAT, &byte, huge_length) == -1);

    WC_TEST_ASSERT(errno == ENOMEM);
    WC_TEST_ASSERT(queue.head == NULL);
    WC_TEST_ASSERT(queue.tail == NULL);
    WC_TEST_ASSERT(queue.length == 0);
    return 0;
}
