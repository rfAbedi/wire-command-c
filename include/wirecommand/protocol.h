#ifndef WIRECOMMAND_PROTOCOL_H
#define WIRECOMMAND_PROTOCOL_H

#include "wirecommand/buffer.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Request layout:
 *   2 bytes: total request length, including this six-byte header
 *   2 bytes: request type
 *   2 bytes: argument length
 *   remaining bytes: raw argument
 *
 * Response layout:
 *   2 bytes: response data length, excluding this two-byte header
 *   remaining bytes: raw response data
 *
 * Every two-byte field uses network byte order.
 */
enum {
    WC_PROTOCOL_REQUEST_LENGTH_OFFSET = 0,
    WC_PROTOCOL_REQUEST_TYPE_OFFSET = 2,
    WC_PROTOCOL_REQUEST_ARGUMENT_LENGTH_OFFSET = 4,
    WC_PROTOCOL_REQUEST_HEADER_SIZE = 6,
    WC_PROTOCOL_RESPONSE_LENGTH_OFFSET = 0,
    WC_PROTOCOL_RESPONSE_HEADER_SIZE = 2
};

enum wc_request_type {
    WC_REQUEST_LS = 1,
    WC_REQUEST_PWD = 2,
    WC_REQUEST_CAT = 3
};

enum wc_parse_result {
    WC_PARSE_COMPLETE,
    WC_PARSE_NEED_MORE,
    WC_PARSE_INVALID
};

struct wc_request {
    enum wc_request_type type;
    const unsigned char *argument;
    size_t argument_length;
    size_t message_length;
};

struct wc_response {
    const unsigned char *data;
    size_t data_length;
    size_t message_length;
};

/*
 * Encode one request. Argument bytes are copied without a terminating null.
 * Argument must not point inside output because append may move output->data.
 */
int wc_protocol_encode_request(struct wc_buffer *output,
                               enum wc_request_type type,
                               const void *argument, size_t argument_length);

/*
 * Parse the first request in data. A complete request borrows its argument
 * from data. The pointer becomes invalid when data is changed or released.
 * consumed is zero for incomplete and invalid input.
 */
enum wc_parse_result wc_protocol_parse_request(const unsigned char *data,
                                               size_t data_size,
                                               struct wc_request *request,
                                               size_t *consumed);

/*
 * Append one data-only response. Empty response data is valid. Data must not
 * point inside output because append may move output->data.
 */
int wc_protocol_encode_response(struct wc_buffer *output, const void *data,
                                size_t data_length);

/*
 * Parse the first response in data. A complete response borrows response data
 * from the input and reports the complete framed size in consumed.
 */
enum wc_parse_result wc_protocol_parse_response(const unsigned char *data,
                                                size_t data_size,
                                                struct wc_response *response,
                                                size_t *consumed);

#endif
