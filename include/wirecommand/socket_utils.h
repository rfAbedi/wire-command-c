#ifndef WIRECOMMAND_SOCKET_UTILS_H
#define WIRECOMMAND_SOCKET_UTILS_H

#include <stddef.h>

/* Change an existing descriptor to nonblocking mode. */
int wc_socket_set_nonblocking(int descriptor);

/* Prevent a disconnected client from terminating the process on write. */
int wc_socket_ignore_sigpipe(void);

/* Write every byte to a blocking descriptor, retrying interrupted writes. */
int wc_socket_write_all(int descriptor, const void *data, size_t size);

#endif
