#ifndef WIRECOMMAND_BUFFER_H
#define WIRECOMMAND_BUFFER_H

#include <stddef.h>

struct wc_buffer {
    unsigned char *data;
    size_t length;
    size_t capacity;
    size_t max_size;
};

/*
 * Initialize an empty buffer. No memory is allocated until the first append.
 * The caller owns the buffer and must eventually call wc_buffer_destroy().
 */
int wc_buffer_init(struct wc_buffer *buffer, size_t max_size);

/*
 * Append bytes without exceeding max_size.
 * Existing data is preserved. A successful append may move buffer->data, so
 * pointers into the old allocation must not be kept across this call.
 */
int wc_buffer_append(struct wc_buffer *buffer, const void *data, size_t size);

/* Remove size bytes from the front and keep the remaining bytes in order. */
int wc_buffer_consume(struct wc_buffer *buffer, size_t size);

/* Release the allocation owned by buffer and return it to the empty state. */
void wc_buffer_destroy(struct wc_buffer *buffer);

#endif
