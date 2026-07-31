#ifndef WIRECOMMAND_COMMANDS_H
#define WIRECOMMAND_COMMANDS_H

#include <stddef.h>

struct wc_command_result {
    unsigned char *data;
    size_t length;
};

/*
 * Each command copies its output into memory owned by result. The caller must
 * release a successful result with wc_command_result_destroy(). Arguments are
 * borrowed raw bytes and are shell-quoted before the temporary command runs.
 */
int wc_command_ls(const unsigned char *argument, size_t argument_length,
                  size_t max_result_size, struct wc_command_result *result);
int wc_command_pwd(size_t max_result_size, struct wc_command_result *result);
int wc_command_cat(const unsigned char *argument, size_t argument_length,
                   size_t max_result_size, struct wc_command_result *result);

void wc_command_result_destroy(struct wc_command_result *result);

#endif
