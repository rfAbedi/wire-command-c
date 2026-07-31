#include "test.h"

#include "wirecommand/buffer.h"
#include "wirecommand/protocol.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int test_protocol_request_empty_argument_is_encoded(void)
{
    struct wc_buffer output;
    const unsigned char expected[] = {0, 6, 0, WC_REQUEST_PWD, 0, 0};

    WC_TEST_ASSERT(wc_buffer_init(&output, 32) == 0);

    WC_TEST_ASSERT(wc_protocol_encode_request(
                       &output, WC_REQUEST_PWD, NULL, 0) == 0);

    WC_TEST_ASSERT(output.length == sizeof(expected));
    WC_TEST_ASSERT(memcmp(output.data, expected, sizeof(expected)) == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_request_argument_is_encoded_without_null(void)
{
    struct wc_buffer output;
    const char argument[] = "/root";
    const unsigned char expected[] = {
        0, 11, 0, WC_REQUEST_LS, 0, 5, '/', 'r', 'o', 'o', 't'
    };

    WC_TEST_ASSERT(wc_buffer_init(&output, 32) == 0);

    WC_TEST_ASSERT(wc_protocol_encode_request(
                       &output, WC_REQUEST_LS, argument,
                       sizeof(argument) - 1) == 0);

    WC_TEST_ASSERT(output.length == sizeof(expected));
    WC_TEST_ASSERT(memcmp(output.data, expected, sizeof(expected)) == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_request_large_lengths_use_network_order(void)
{
    struct wc_buffer output;
    unsigned char argument[300] = {0};

    WC_TEST_ASSERT(wc_buffer_init(&output, 400) == 0);

    WC_TEST_ASSERT(wc_protocol_encode_request(
                       &output, WC_REQUEST_CAT, argument,
                       sizeof(argument)) == 0);

    WC_TEST_ASSERT(output.data[0] == 1);
    WC_TEST_ASSERT(output.data[1] == 50);
    WC_TEST_ASSERT(output.data[2] == 0);
    WC_TEST_ASSERT(output.data[3] == WC_REQUEST_CAT);
    WC_TEST_ASSERT(output.data[4] == 1);
    WC_TEST_ASSERT(output.data[5] == 44);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_invalid_request_type_is_not_encoded(void)
{
    struct wc_buffer output;

    WC_TEST_ASSERT(wc_buffer_init(&output, 32) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_protocol_encode_request(
                       &output, (enum wc_request_type)99, NULL, 0) == -1);

    WC_TEST_ASSERT(errno == EINVAL);
    WC_TEST_ASSERT(output.length == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_oversized_request_argument_is_rejected(void)
{
    struct wc_buffer output;
    unsigned char byte = 1;

    WC_TEST_ASSERT(wc_buffer_init(&output, SIZE_MAX) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_protocol_encode_request(
                       &output, WC_REQUEST_CAT, &byte, UINT16_MAX) == -1);

    WC_TEST_ASSERT(errno == EMSGSIZE);
    WC_TEST_ASSERT(output.length == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_request_beyond_output_limit_is_rejected(void)
{
    struct wc_buffer output;

    WC_TEST_ASSERT(wc_buffer_init(
                       &output, WC_PROTOCOL_REQUEST_HEADER_SIZE - 1) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_protocol_encode_request(
                       &output, WC_REQUEST_PWD, NULL, 0) == -1);

    WC_TEST_ASSERT(errno == EMSGSIZE);
    WC_TEST_ASSERT(output.length == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_failed_request_preserves_existing_output(void)
{
    struct wc_buffer output;
    const unsigned char existing[] = {7, 8};

    WC_TEST_ASSERT(wc_buffer_init(&output, 7) == 0);
    WC_TEST_ASSERT(wc_buffer_append(
                       &output, existing, sizeof(existing)) == 0);

    WC_TEST_ASSERT(wc_protocol_encode_request(
                       &output, WC_REQUEST_PWD, NULL, 0) == -1);

    WC_TEST_ASSERT(output.length == sizeof(existing));
    WC_TEST_ASSERT(memcmp(output.data, existing, sizeof(existing)) == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_zero_through_five_request_bytes_need_more(void)
{
    const unsigned char input[] = {0, 6, 0, WC_REQUEST_PWD, 0, 0};
    struct wc_request request;
    size_t available;

    for (available = 0; available < WC_PROTOCOL_REQUEST_HEADER_SIZE;
         ++available) {
        size_t consumed = 99;

        WC_TEST_ASSERT(wc_protocol_parse_request(
                           input, available, &request, &consumed) ==
                       WC_PARSE_NEED_MORE);
        WC_TEST_ASSERT(consumed == 0);
    }
    return 0;
}

int test_protocol_fragmented_request_argument_needs_more(void)
{
    const unsigned char input[] = {
        0, 9, 0, WC_REQUEST_CAT, 0, 3, '/', 't'
    };
    struct wc_request request;
    size_t consumed = 99;

    WC_TEST_ASSERT(wc_protocol_parse_request(input, sizeof(input), &request,
                                             &consumed) ==
                   WC_PARSE_NEED_MORE);
    WC_TEST_ASSERT(consumed == 0);
    return 0;
}

int test_protocol_unknown_request_type_is_rejected(void)
{
    const unsigned char input[] = {0, 6, 0, 99, 0, 0};
    struct wc_request request;
    size_t consumed = 99;

    WC_TEST_ASSERT(wc_protocol_parse_request(input, sizeof(input), &request,
                                             &consumed) ==
                   WC_PARSE_INVALID);
    WC_TEST_ASSERT(consumed == 0);
    return 0;
}

int test_protocol_request_length_smaller_than_header_is_rejected(void)
{
    const unsigned char input[] = {0, 5, 0, WC_REQUEST_PWD, 0, 0};
    struct wc_request request;
    size_t consumed = 99;

    WC_TEST_ASSERT(wc_protocol_parse_request(input, sizeof(input), &request,
                                             &consumed) ==
                   WC_PARSE_INVALID);
    WC_TEST_ASSERT(consumed == 0);
    return 0;
}

int test_protocol_request_length_mismatch_is_rejected(void)
{
    const unsigned char input[] = {0, 8, 0, WC_REQUEST_LS, 0, 1, '/'};
    struct wc_request request;
    size_t consumed = 99;

    WC_TEST_ASSERT(wc_protocol_parse_request(input, sizeof(input), &request,
                                             &consumed) ==
                   WC_PARSE_INVALID);
    WC_TEST_ASSERT(consumed == 0);
    return 0;
}

int test_protocol_complete_request_returns_argument_view(void)
{
    const unsigned char input[] = {
        0, 10, 0, WC_REQUEST_LS, 0, 4, '/', 't', 'm', 'p'
    };
    struct wc_request request;
    size_t consumed = 0;

    WC_TEST_ASSERT(wc_protocol_parse_request(input, sizeof(input), &request,
                                             &consumed) ==
                   WC_PARSE_COMPLETE);
    WC_TEST_ASSERT(request.type == WC_REQUEST_LS);
    WC_TEST_ASSERT(request.argument == input + 6);
    WC_TEST_ASSERT(request.argument_length == 4);
    WC_TEST_ASSERT(memcmp(request.argument, "/tmp", 4) == 0);
    WC_TEST_ASSERT(request.message_length == sizeof(input));
    WC_TEST_ASSERT(consumed == sizeof(input));
    return 0;
}

int test_protocol_two_requests_parse_in_order(void)
{
    const unsigned char input[] = {
        0, 6, 0, WC_REQUEST_PWD, 0, 0,
        0, 8, 0, WC_REQUEST_CAT, 0, 2, '/', 'a'
    };
    struct wc_request first;
    struct wc_request second;
    size_t first_size = 0;
    size_t second_size = 0;

    WC_TEST_ASSERT(wc_protocol_parse_request(input, sizeof(input), &first,
                                             &first_size) ==
                   WC_PARSE_COMPLETE);
    WC_TEST_ASSERT(wc_protocol_parse_request(input + first_size,
                                             sizeof(input) - first_size,
                                             &second, &second_size) ==
                   WC_PARSE_COMPLETE);

    WC_TEST_ASSERT(first.type == WC_REQUEST_PWD);
    WC_TEST_ASSERT(second.type == WC_REQUEST_CAT);
    WC_TEST_ASSERT(first_size + second_size == sizeof(input));
    return 0;
}

int test_protocol_complete_request_before_partial_request_parses(void)
{
    const unsigned char input[] = {
        0, 6, 0, WC_REQUEST_PWD, 0, 0,
        0, 8, 0
    };
    struct wc_request request;
    size_t first_size = 0;
    size_t second_size = 99;

    WC_TEST_ASSERT(wc_protocol_parse_request(input, sizeof(input), &request,
                                             &first_size) ==
                   WC_PARSE_COMPLETE);
    WC_TEST_ASSERT(wc_protocol_parse_request(input + first_size,
                                             sizeof(input) - first_size,
                                             &request, &second_size) ==
                   WC_PARSE_NEED_MORE);
    WC_TEST_ASSERT(first_size == 6);
    WC_TEST_ASSERT(second_size == 0);
    return 0;
}

int test_protocol_response_examples_are_encoded(void)
{
    struct wc_buffer output;
    const unsigned char expected[] = {
        0, 1, '/',
        0, 5, '/', 'r', 'o', 'o', 't',
        0, 0
    };

    WC_TEST_ASSERT(wc_buffer_init(&output, 32) == 0);

    WC_TEST_ASSERT(wc_protocol_encode_response(&output, "/", 1) == 0);
    WC_TEST_ASSERT(wc_protocol_encode_response(&output, "/root", 5) == 0);
    WC_TEST_ASSERT(wc_protocol_encode_response(&output, NULL, 0) == 0);

    WC_TEST_ASSERT(output.length == sizeof(expected));
    WC_TEST_ASSERT(memcmp(output.data, expected, sizeof(expected)) == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_maximum_response_data_is_encoded(void)
{
    struct wc_buffer output;
    unsigned char *data = malloc(UINT16_MAX);

    WC_TEST_ASSERT(data != NULL);
    memset(data, 'x', UINT16_MAX);
    WC_TEST_ASSERT(wc_buffer_init(
                       &output,
                       WC_PROTOCOL_RESPONSE_HEADER_SIZE + UINT16_MAX) == 0);

    WC_TEST_ASSERT(wc_protocol_encode_response(
                       &output, data, UINT16_MAX) == 0);

    WC_TEST_ASSERT(output.length ==
                   WC_PROTOCOL_RESPONSE_HEADER_SIZE + UINT16_MAX);
    WC_TEST_ASSERT(output.data[0] == 0xff);
    WC_TEST_ASSERT(output.data[1] == 0xff);
    WC_TEST_ASSERT(output.data[2] == 'x');

    wc_buffer_destroy(&output);
    free(data);
    return 0;
}

int test_protocol_oversized_response_data_is_rejected(void)
{
    struct wc_buffer output;
    unsigned char byte = 1;

    WC_TEST_ASSERT(wc_buffer_init(&output, SIZE_MAX) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_protocol_encode_response(
                       &output, &byte, (size_t)UINT16_MAX + 1) == -1);

    WC_TEST_ASSERT(errno == EMSGSIZE);
    WC_TEST_ASSERT(output.length == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_response_beyond_output_limit_is_rejected(void)
{
    struct wc_buffer output;

    WC_TEST_ASSERT(wc_buffer_init(
                       &output, WC_PROTOCOL_RESPONSE_HEADER_SIZE) == 0);

    errno = 0;
    WC_TEST_ASSERT(wc_protocol_encode_response(&output, "/", 1) == -1);

    WC_TEST_ASSERT(errno == EMSGSIZE);
    WC_TEST_ASSERT(output.length == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_failed_response_preserves_existing_output(void)
{
    struct wc_buffer output;
    const unsigned char existing[] = {7, 8};

    WC_TEST_ASSERT(wc_buffer_init(&output, 4) == 0);
    WC_TEST_ASSERT(wc_buffer_append(
                       &output, existing, sizeof(existing)) == 0);

    WC_TEST_ASSERT(wc_protocol_encode_response(&output, "/", 1) == -1);

    WC_TEST_ASSERT(output.length == sizeof(existing));
    WC_TEST_ASSERT(memcmp(output.data, existing, sizeof(existing)) == 0);

    wc_buffer_destroy(&output);
    return 0;
}

int test_protocol_zero_or_one_response_bytes_need_more(void)
{
    const unsigned char input[] = {0, 0};
    struct wc_response response;
    size_t available;

    for (available = 0; available < WC_PROTOCOL_RESPONSE_HEADER_SIZE;
         ++available) {
        size_t consumed = 99;

        WC_TEST_ASSERT(wc_protocol_parse_response(
                           input, available, &response, &consumed) ==
                       WC_PARSE_NEED_MORE);
        WC_TEST_ASSERT(consumed == 0);
    }
    return 0;
}

int test_protocol_partial_response_data_needs_more(void)
{
    const unsigned char input[] = {0, 2, 'o'};
    struct wc_response response;
    size_t consumed = 99;

    WC_TEST_ASSERT(wc_protocol_parse_response(input, sizeof(input), &response,
                                              &consumed) ==
                   WC_PARSE_NEED_MORE);
    WC_TEST_ASSERT(consumed == 0);
    return 0;
}

int test_protocol_zero_length_response_is_complete(void)
{
    const unsigned char input[] = {0, 0};
    struct wc_response response;
    size_t consumed = 0;

    WC_TEST_ASSERT(wc_protocol_parse_response(input, sizeof(input), &response,
                                              &consumed) ==
                   WC_PARSE_COMPLETE);
    WC_TEST_ASSERT(response.data == input + 2);
    WC_TEST_ASSERT(response.data_length == 0);
    WC_TEST_ASSERT(response.message_length == 2);
    WC_TEST_ASSERT(consumed == 2);
    return 0;
}

int test_protocol_complete_response_returns_data_view(void)
{
    const unsigned char input[] = {0, 5, '/', 'r', 'o', 'o', 't'};
    struct wc_response response;
    size_t consumed = 0;

    WC_TEST_ASSERT(wc_protocol_parse_response(input, sizeof(input), &response,
                                              &consumed) ==
                   WC_PARSE_COMPLETE);
    WC_TEST_ASSERT(response.data == input + 2);
    WC_TEST_ASSERT(response.data_length == 5);
    WC_TEST_ASSERT(memcmp(response.data, "/root", 5) == 0);
    WC_TEST_ASSERT(response.message_length == sizeof(input));
    WC_TEST_ASSERT(consumed == sizeof(input));
    return 0;
}

int test_protocol_two_responses_parse_in_order(void)
{
    const unsigned char input[] = {0, 1, '/', 0, 2, 'o', 'k'};
    struct wc_response first;
    struct wc_response second;
    size_t first_size = 0;
    size_t second_size = 0;

    WC_TEST_ASSERT(wc_protocol_parse_response(input, sizeof(input), &first,
                                              &first_size) ==
                   WC_PARSE_COMPLETE);
    WC_TEST_ASSERT(wc_protocol_parse_response(input + first_size,
                                              sizeof(input) - first_size,
                                              &second, &second_size) ==
                   WC_PARSE_COMPLETE);

    WC_TEST_ASSERT(first.data[0] == '/');
    WC_TEST_ASSERT(second.data_length == 2);
    WC_TEST_ASSERT(first_size + second_size == sizeof(input));
    return 0;
}

int test_protocol_complete_response_before_partial_response_parses(void)
{
    const unsigned char input[] = {0, 1, '/', 0, 2, 'o'};
    struct wc_response response;
    size_t first_size = 0;
    size_t second_size = 99;

    WC_TEST_ASSERT(wc_protocol_parse_response(input, sizeof(input), &response,
                                              &first_size) ==
                   WC_PARSE_COMPLETE);
    WC_TEST_ASSERT(wc_protocol_parse_response(input + first_size,
                                              sizeof(input) - first_size,
                                              &response, &second_size) ==
                   WC_PARSE_NEED_MORE);
    WC_TEST_ASSERT(first_size == 3);
    WC_TEST_ASSERT(second_size == 0);
    return 0;
}
