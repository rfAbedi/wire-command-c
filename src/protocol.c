#include "wirecommand/protocol.h"

#include <errno.h>
#include <stdint.h>

static uint16_t wc_protocol_decode_u16(const unsigned char *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static void wc_protocol_encode_u16(unsigned char *destination, uint16_t value)
{
    destination[0] = (unsigned char)(value >> 8);
    destination[1] = (unsigned char)value;
}

static int wc_protocol_request_type_valid(int type)
{
    return type == WC_REQUEST_LS || type == WC_REQUEST_PWD ||
           type == WC_REQUEST_CAT;
}

int wc_protocol_encode_request(struct wc_buffer *output,
                               enum wc_request_type type,
                               const void *argument, size_t argument_length)
{
    unsigned char header[WC_PROTOCOL_REQUEST_HEADER_SIZE];
    size_t message_length;
    size_t original_length;

    if (output == NULL || (argument == NULL && argument_length != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (!wc_protocol_request_type_valid(type)) {
        errno = EINVAL;
        return -1;
    }
    if (argument_length > UINT16_MAX - WC_PROTOCOL_REQUEST_HEADER_SIZE) {
        errno = EMSGSIZE;
        return -1;
    }

    message_length = WC_PROTOCOL_REQUEST_HEADER_SIZE + argument_length;
    if (output->length > output->max_size ||
        message_length > output->max_size - output->length) {
        errno = EMSGSIZE;
        return -1;
    }

    wc_protocol_encode_u16(
        header + WC_PROTOCOL_REQUEST_LENGTH_OFFSET,
        (uint16_t)message_length);
    wc_protocol_encode_u16(header + WC_PROTOCOL_REQUEST_TYPE_OFFSET,
                           (uint16_t)type);
    wc_protocol_encode_u16(
        header + WC_PROTOCOL_REQUEST_ARGUMENT_LENGTH_OFFSET,
        (uint16_t)argument_length);

    original_length = output->length;
    if (wc_buffer_append(output, header, sizeof(header)) == -1) {
        return -1;
    }
    if (wc_buffer_append(output, argument, argument_length) == -1) {
        output->length = original_length;
        return -1;
    }

    return 0;
}

enum wc_parse_result wc_protocol_parse_request(const unsigned char *data,
                                               size_t data_size,
                                               struct wc_request *request,
                                               size_t *consumed)
{
    uint16_t request_length;
    uint16_t request_type;
    uint16_t argument_length;

    if (request == NULL || consumed == NULL ||
        (data == NULL && data_size != 0)) {
        return WC_PARSE_INVALID;
    }

    *consumed = 0;
    if (data_size < WC_PROTOCOL_REQUEST_HEADER_SIZE) {
        return WC_PARSE_NEED_MORE;
    }

    request_length = wc_protocol_decode_u16(
        data + WC_PROTOCOL_REQUEST_LENGTH_OFFSET);
    request_type =
        wc_protocol_decode_u16(data + WC_PROTOCOL_REQUEST_TYPE_OFFSET);
    argument_length = wc_protocol_decode_u16(
        data + WC_PROTOCOL_REQUEST_ARGUMENT_LENGTH_OFFSET);

    if (!wc_protocol_request_type_valid(request_type)) {
        return WC_PARSE_INVALID;
    }
    if (request_length < WC_PROTOCOL_REQUEST_HEADER_SIZE) {
        return WC_PARSE_INVALID;
    }
    if ((size_t)request_length !=
        WC_PROTOCOL_REQUEST_HEADER_SIZE + (size_t)argument_length) {
        return WC_PARSE_INVALID;
    }
    if (data_size < request_length) {
        return WC_PARSE_NEED_MORE;
    }

    request->type = (enum wc_request_type)request_type;
    request->argument = data + WC_PROTOCOL_REQUEST_HEADER_SIZE;
    request->argument_length = argument_length;
    request->message_length = request_length;
    *consumed = request_length;
    return WC_PARSE_COMPLETE;
}

int wc_protocol_encode_response(struct wc_buffer *output, const void *data,
                                size_t data_length)
{
    unsigned char header[WC_PROTOCOL_RESPONSE_HEADER_SIZE];
    size_t message_length;
    size_t original_length;

    if (output == NULL || (data == NULL && data_length != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (data_length > UINT16_MAX) {
        errno = EMSGSIZE;
        return -1;
    }

    message_length = WC_PROTOCOL_RESPONSE_HEADER_SIZE + data_length;
    if (output->length > output->max_size ||
        message_length > output->max_size - output->length) {
        errno = EMSGSIZE;
        return -1;
    }

    wc_protocol_encode_u16(header + WC_PROTOCOL_RESPONSE_LENGTH_OFFSET,
                           (uint16_t)data_length);

    original_length = output->length;
    if (wc_buffer_append(output, header, sizeof(header)) == -1) {
        return -1;
    }
    if (wc_buffer_append(output, data, data_length) == -1) {
        output->length = original_length;
        return -1;
    }

    return 0;
}

enum wc_parse_result wc_protocol_parse_response(const unsigned char *data,
                                                size_t data_size,
                                                struct wc_response *response,
                                                size_t *consumed)
{
    uint16_t response_length;
    size_t message_length;

    if (response == NULL || consumed == NULL ||
        (data == NULL && data_size != 0)) {
        return WC_PARSE_INVALID;
    }

    *consumed = 0;
    if (data_size < WC_PROTOCOL_RESPONSE_HEADER_SIZE) {
        return WC_PARSE_NEED_MORE;
    }

    response_length = wc_protocol_decode_u16(
        data + WC_PROTOCOL_RESPONSE_LENGTH_OFFSET);
    message_length = WC_PROTOCOL_RESPONSE_HEADER_SIZE + response_length;

    if (data_size < message_length) {
        return WC_PARSE_NEED_MORE;
    }

    response->data = data + WC_PROTOCOL_RESPONSE_HEADER_SIZE;
    response->data_length = response_length;
    response->message_length = message_length;
    *consumed = message_length;
    return WC_PARSE_COMPLETE;
}
