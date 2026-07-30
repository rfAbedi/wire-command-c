#include "test.h"

#include "wirecommand/buffer.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

int test_buffer_initial_state_is_empty(void)
{
    struct wc_buffer buffer;

    WC_TEST_ASSERT(wc_buffer_init(&buffer, 128) == 0);

    WC_TEST_ASSERT(buffer.data == NULL);
    WC_TEST_ASSERT(buffer.length == 0);
    WC_TEST_ASSERT(buffer.capacity == 0);
    WC_TEST_ASSERT(buffer.max_size == 128);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_appended_bytes_are_preserved(void)
{
    struct wc_buffer buffer;
    const unsigned char first[] = {1, 2, 3};
    const unsigned char second[] = {4, 5};
    const unsigned char expected[] = {1, 2, 3, 4, 5};

    WC_TEST_ASSERT(wc_buffer_init(&buffer, 128) == 0);

    WC_TEST_ASSERT(wc_buffer_append(&buffer, first, sizeof(first)) == 0);
    WC_TEST_ASSERT(wc_buffer_append(&buffer, second, sizeof(second)) == 0);

    WC_TEST_ASSERT(buffer.length == sizeof(expected));
    WC_TEST_ASSERT(memcmp(buffer.data, expected, sizeof(expected)) == 0);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_non_character_value_is_preserved(void)
{
    struct wc_buffer buffer;
    uint32_t input = UINT32_C(0x12345678);
    uint32_t output = 0;

    WC_TEST_ASSERT(wc_buffer_init(&buffer, sizeof(input)) == 0);

    WC_TEST_ASSERT(wc_buffer_append(&buffer, &input, sizeof(input)) == 0);
    memcpy(&output, buffer.data, sizeof(output));

    WC_TEST_ASSERT(buffer.length == sizeof(input));
    WC_TEST_ASSERT(output == input);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_large_append_grows_capacity(void)
{
    struct wc_buffer buffer;
    unsigned char input[100];
    size_t index;

    for (index = 0; index < sizeof(input); ++index) {
        input[index] = (unsigned char)index;
    }
    WC_TEST_ASSERT(wc_buffer_init(&buffer, 200) == 0);

    WC_TEST_ASSERT(wc_buffer_append(&buffer, input, sizeof(input)) == 0);

    WC_TEST_ASSERT(buffer.capacity >= sizeof(input));
    WC_TEST_ASSERT(buffer.capacity <= buffer.max_size);
    WC_TEST_ASSERT(memcmp(buffer.data, input, sizeof(input)) == 0);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_consumed_prefix_is_removed(void)
{
    struct wc_buffer buffer;
    const char input[] = "abcdef";

    WC_TEST_ASSERT(wc_buffer_init(&buffer, 32) == 0);
    WC_TEST_ASSERT(wc_buffer_append(&buffer, input, 6) == 0);

    WC_TEST_ASSERT(wc_buffer_consume(&buffer, 2) == 0);

    WC_TEST_ASSERT(buffer.length == 4);
    WC_TEST_ASSERT(memcmp(buffer.data, "cdef", 4) == 0);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_consuming_all_bytes_makes_it_empty(void)
{
    struct wc_buffer buffer;
    const char input[] = "data";

    WC_TEST_ASSERT(wc_buffer_init(&buffer, 16) == 0);
    WC_TEST_ASSERT(wc_buffer_append(&buffer, input, 4) == 0);

    WC_TEST_ASSERT(wc_buffer_consume(&buffer, 4) == 0);

    WC_TEST_ASSERT(buffer.length == 0);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_empty_append_changes_nothing(void)
{
    struct wc_buffer buffer;

    WC_TEST_ASSERT(wc_buffer_init(&buffer, 16) == 0);

    WC_TEST_ASSERT(wc_buffer_append(&buffer, NULL, 0) == 0);

    WC_TEST_ASSERT(buffer.data == NULL);
    WC_TEST_ASSERT(buffer.length == 0);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_append_beyond_limit_is_rejected(void)
{
    struct wc_buffer buffer;
    const char input[] = "12345";

    WC_TEST_ASSERT(wc_buffer_init(&buffer, 4) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_buffer_append(&buffer, input, 5) == -1);

    WC_TEST_ASSERT(errno == EMSGSIZE);
    WC_TEST_ASSERT(buffer.length == 0);
    WC_TEST_ASSERT(buffer.data == NULL);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_overflowing_append_is_rejected(void)
{
    struct wc_buffer buffer;
    unsigned char byte = 1;

    WC_TEST_ASSERT(wc_buffer_init(&buffer, SIZE_MAX) == 0);
    WC_TEST_ASSERT(wc_buffer_append(&buffer, &byte, 1) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_buffer_append(&buffer, &byte, SIZE_MAX) == -1);

    WC_TEST_ASSERT(errno == EMSGSIZE);
    WC_TEST_ASSERT(buffer.length == 1);
    WC_TEST_ASSERT(buffer.data[0] == byte);

    wc_buffer_destroy(&buffer);
    return 0;
}

int test_buffer_consume_beyond_length_is_rejected(void)
{
    struct wc_buffer buffer;

    WC_TEST_ASSERT(wc_buffer_init(&buffer, 16) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_buffer_consume(&buffer, 1) == -1);

    WC_TEST_ASSERT(errno == EINVAL);
    WC_TEST_ASSERT(buffer.length == 0);

    wc_buffer_destroy(&buffer);
    return 0;
}
