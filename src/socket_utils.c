#define _POSIX_C_SOURCE 200809L

#include "wirecommand/socket_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <unistd.h>

int wc_socket_set_nonblocking(int descriptor)
{
    int flags;

    do {
        flags = fcntl(descriptor, F_GETFL);
    } while (flags == -1 && errno == EINTR);
    if (flags == -1) {
        return -1;
    }

    if ((flags & O_NONBLOCK) != 0) {
        return 0;
    }

    do {
        flags = fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
    } while (flags == -1 && errno == EINTR);
    return flags == -1 ? -1 : 0;
}

int wc_socket_ignore_sigpipe(void)
{
    struct sigaction action = {0};

    action.sa_handler = SIG_IGN;
    if (sigemptyset(&action.sa_mask) == -1) {
        return -1;
    }
    return sigaction(SIGPIPE, &action, NULL);
}

int wc_socket_write_all(int descriptor, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t total_written = 0;

    if (data == NULL && size != 0) {
        errno = EINVAL;
        return -1;
    }

    while (total_written < size) {
        ssize_t bytes_written =
            write(descriptor, bytes + total_written, size - total_written);

        if (bytes_written > 0) {
            total_written += (size_t)bytes_written;
        } else if (bytes_written == -1 && errno == EINTR) {
            continue;
        } else {
            if (bytes_written == 0) {
                errno = EIO;
            }
            return -1;
        }
    }
    return 0;
}
