#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "wirecommand/socket_utils.h"

#include <sys/socket.h>
#include <unistd.h>

int test_socket_utils_write_all_sends_every_byte(void)
{
    static const unsigned char expected[] = {0x41, 0x00, 0xff, 0x42};
    unsigned char received[sizeof(expected)];
    int sockets[2];
    size_t total_read = 0;

    WC_TEST_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    WC_TEST_ASSERT(
        wc_socket_write_all(sockets[0], expected, sizeof(expected)) == 0);
    while (total_read < sizeof(received)) {
        ssize_t bytes_read = read(sockets[1], received + total_read,
                                  sizeof(received) - total_read);

        WC_TEST_ASSERT(bytes_read > 0);
        total_read += (size_t)bytes_read;
    }
    WC_TEST_ASSERT(memcmp(received, expected, sizeof(expected)) == 0);
    WC_TEST_ASSERT(close(sockets[0]) == 0);
    WC_TEST_ASSERT(close(sockets[1]) == 0);
    return 0;
}
