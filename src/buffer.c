#include "wirecommand/buffer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum {
    WC_BUFFER_INITIAL_CAPACITY = 64
};

/*
 * Grow only when an append needs more space. Capacity doubles to avoid a
 * realloc() call for every small append, but never exceeds max_size.
 */
static int wc_buffer_grow(struct wc_buffer *buffer, size_t required_capacity)
{
    size_t new_capacity = buffer->capacity;
    unsigned char *new_data;

    if (required_capacity <= buffer->capacity) {
        return 0;
    }

    if (new_capacity == 0) {
        new_capacity = WC_BUFFER_INITIAL_CAPACITY;
    }
    if (new_capacity > buffer->max_size) {
        new_capacity = buffer->max_size;
    }

    while (new_capacity < required_capacity) {
        if (new_capacity > buffer->max_size / 2) {
            new_capacity = buffer->max_size;
        } else {
            new_capacity *= 2;
        }
    }

    new_data = realloc(buffer->data, new_capacity);
    if (new_data == NULL) {
        errno = ENOMEM;
        return -1;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return 0;
}

int wc_buffer_init(struct wc_buffer *buffer, size_t max_size)
{
    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    buffer->max_size = max_size;
    return 0;
}

int wc_buffer_append(struct wc_buffer *buffer, const void *data, size_t size)
{
    size_t required_capacity;

    if (buffer == NULL || (data == NULL && size != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (size > buffer->max_size - buffer->length) {
        errno = EMSGSIZE;
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    required_capacity = buffer->length + size;
    if (wc_buffer_grow(buffer, required_capacity) == -1) {
        return -1;
    }

    memcpy(buffer->data + buffer->length, data, size);
    buffer->length += size;
    return 0;
}

int wc_buffer_consume(struct wc_buffer *buffer, size_t size)
{
    size_t remaining;

    if (buffer == NULL || size > buffer->length) {
        errno = EINVAL;
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    remaining = buffer->length - size;
    memmove(buffer->data, buffer->data + size, remaining);
    buffer->length = remaining;
    return 0;
}

void wc_buffer_destroy(struct wc_buffer *buffer)
{
    if (buffer == NULL) {
        return;
    }

    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    buffer->max_size = 0;
}
