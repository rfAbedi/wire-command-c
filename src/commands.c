#define _POSIX_C_SOURCE 200809L

#include "wirecommand/commands.h"

#include "wirecommand/buffer.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

enum {
    WC_COMMAND_READ_SIZE = 4096
};

/* Quote raw path bytes as one shell argument. */
static char *wc_command_quote_argument(const unsigned char *argument,
                                       size_t argument_length)
{
    char *quoted;
    size_t input_index;
    size_t output_index = 0;

    if (argument == NULL || argument_length == 0 ||
        memchr(argument, '\0', argument_length) != NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (argument_length > (SIZE_MAX - 3) / 4) {
        errno = EOVERFLOW;
        return NULL;
    }

    quoted = malloc(argument_length * 4 + 3);
    if (quoted == NULL) {
        return NULL;
    }

    quoted[output_index++] = '\'';
    for (input_index = 0; input_index < argument_length; ++input_index) {
        if (argument[input_index] == '\'') {
            quoted[output_index++] = '\'';
            quoted[output_index++] = '\\';
            quoted[output_index++] = '\'';
            quoted[output_index++] = '\'';
        } else {
            quoted[output_index++] = (char)argument[input_index];
        }
    }
    quoted[output_index++] = '\'';
    quoted[output_index] = '\0';
    return quoted;
}

static char *wc_command_build(const char *program,
                              const unsigned char *argument,
                              size_t argument_length)
{
    static const char suffix[] = " 2>/dev/null";
    char *quoted_argument;
    char *command;
    size_t command_length;

    quoted_argument = wc_command_quote_argument(argument, argument_length);
    if (quoted_argument == NULL) {
        return NULL;
    }

    command_length = strlen(program) + 1 + strlen(quoted_argument) +
                     sizeof(suffix);
    command = malloc(command_length);
    if (command == NULL) {
        free(quoted_argument);
        return NULL;
    }

    (void)snprintf(command, command_length, "%s %s%s", program,
                   quoted_argument, suffix);
    free(quoted_argument);
    return command;
}

static int wc_command_run(const char *command, size_t max_result_size,
                          struct wc_command_result *result)
{
    FILE *pipe;
    struct wc_buffer output;
    int command_failed = 0;
    int failure_errno = 0;
    int child_status;

    pipe = popen(command, "r");
    if (pipe == NULL) {
        return -1;
    }
    if (wc_buffer_init(&output, max_result_size) == -1) {
        int saved_errno = errno;

        (void)pclose(pipe);
        errno = saved_errno;
        return -1;
    }

    for (;;) {
        unsigned char chunk[WC_COMMAND_READ_SIZE];
        size_t bytes_read = fread(chunk, 1, sizeof(chunk), pipe);

        if (bytes_read > 0 &&
            wc_buffer_append(&output, chunk, bytes_read) == -1) {
            command_failed = 1;
            failure_errno = errno;
            break;
        }
        if (bytes_read < sizeof(chunk)) {
            if (ferror(pipe)) {
                command_failed = 1;
                failure_errno = EIO;
            }
            break;
        }
    }

    child_status = pclose(pipe);
    if (child_status == -1 || !WIFEXITED(child_status) ||
        WEXITSTATUS(child_status) != 0) {
        if (!command_failed) {
            failure_errno = EIO;
        }
        command_failed = 1;
    }
    if (command_failed) {
        wc_buffer_destroy(&output);
        errno = failure_errno;
        return -1;
    }

    result->data = output.data;
    result->length = output.length;
    return 0;
}

int wc_command_ls(const unsigned char *argument, size_t argument_length,
                  size_t max_result_size, struct wc_command_result *result)
{
    char *command;
    int run_result;

    if (result == NULL) {
        errno = EINVAL;
        return -1;
    }
    result->data = NULL;
    result->length = 0;

    command = wc_command_build("ls -1 --", argument, argument_length);
    if (command == NULL) {
        return -1;
    }
    run_result = wc_command_run(command, max_result_size, result);
    free(command);
    return run_result;
}

int wc_command_pwd(size_t max_result_size, struct wc_command_result *result)
{
    int run_result;

    if (result == NULL) {
        errno = EINVAL;
        return -1;
    }
    result->data = NULL;
    result->length = 0;

    run_result = wc_command_run("pwd 2>/dev/null", max_result_size, result);
    if (run_result == 0 && result->length > 0 &&
        result->data[result->length - 1] == '\n') {
        --result->length;
    }
    return run_result;
}

int wc_command_cat(const unsigned char *argument, size_t argument_length,
                   size_t max_result_size, struct wc_command_result *result)
{
    char *command;
    int run_result;

    if (result == NULL) {
        errno = EINVAL;
        return -1;
    }
    result->data = NULL;
    result->length = 0;

    if (argument == NULL || argument_length == 0 || argument[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    command = wc_command_build("cat --", argument, argument_length);
    if (command == NULL) {
        return -1;
    }
    run_result = wc_command_run(command, max_result_size, result);
    free(command);
    return run_result;
}

void wc_command_result_destroy(struct wc_command_result *result)
{
    if (result == NULL) {
        return;
    }

    free(result->data);
    result->data = NULL;
    result->length = 0;
}
