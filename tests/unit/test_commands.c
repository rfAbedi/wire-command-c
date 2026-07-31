#define _POSIX_C_SOURCE 200809L

#include "test.h"

#include "wirecommand/commands.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int wc_test_write_file(const char *path, const void *data, size_t length)
{
    int descriptor;
    size_t written = 0;

    descriptor = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (descriptor == -1) {
        return -1;
    }

    while (written < length) {
        ssize_t result = write(descriptor,
                               (const unsigned char *)data + written,
                               length - written);

        if (result == -1 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            int saved_errno = result == 0 ? EIO : errno;

            (void)close(descriptor);
            errno = saved_errno;
            return -1;
        }
        written += (size_t)result;
    }

    return close(descriptor);
}

static int wc_test_make_path(char *destination, size_t destination_size,
                             const char *directory, const char *name)
{
    int length = snprintf(destination, destination_size, "%s/%s", directory,
                          name);

    if (length < 0 || (size_t)length >= destination_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int wc_test_result_has_line(const struct wc_command_result *result,
                                   const char *expected)
{
    size_t expected_length = strlen(expected);
    size_t offset = 0;

    while (offset < result->length) {
        const unsigned char *line = result->data + offset;
        const unsigned char *newline =
            memchr(line, '\n', result->length - offset);
        size_t line_length;

        if (newline == NULL) {
            return 0;
        }
        line_length = (size_t)(newline - line);
        if (line_length == expected_length &&
            memcmp(line, expected, expected_length) == 0) {
            return 1;
        }
        offset += line_length + 1;
    }
    return 0;
}

int test_commands_ls_lists_directory_entries(void)
{
    char directory[] = "/tmp/wirecommand-ls-XXXXXX";
    char first_path[256];
    char second_path[256];
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);
    WC_TEST_ASSERT(wc_test_make_path(first_path, sizeof(first_path), directory,
                                     "alpha") == 0);
    WC_TEST_ASSERT(wc_test_make_path(second_path, sizeof(second_path), directory,
                                     "beta") == 0);
    WC_TEST_ASSERT(wc_test_write_file(first_path, "", 0) == 0);
    WC_TEST_ASSERT(wc_test_write_file(second_path, "", 0) == 0);

    WC_TEST_ASSERT(wc_command_ls((const unsigned char *)directory,
                                 strlen(directory), 256, &result) == 0);

    WC_TEST_ASSERT(wc_test_result_has_line(&result, "alpha"));
    WC_TEST_ASSERT(wc_test_result_has_line(&result, "beta"));

    wc_command_result_destroy(&result);
    WC_TEST_ASSERT(unlink(first_path) == 0);
    WC_TEST_ASSERT(unlink(second_path) == 0);
    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}

int test_commands_ls_empty_directory_returns_empty_result(void)
{
    char directory[] = "/tmp/wirecommand-ls-empty-XXXXXX";
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);

    WC_TEST_ASSERT(wc_command_ls((const unsigned char *)directory,
                                 strlen(directory), 0, &result) == 0);

    WC_TEST_ASSERT(result.data == NULL);
    WC_TEST_ASSERT(result.length == 0);

    wc_command_result_destroy(&result);
    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}

int test_commands_ls_missing_directory_returns_error(void)
{
    const char path[] = "/tmp/wirecommand-directory-that-does-not-exist";
    struct wc_command_result result;

    errno = 0;
    WC_TEST_ASSERT(wc_command_ls((const unsigned char *)path,
                                 sizeof(path) - 1, 64, &result) == -1);
    WC_TEST_ASSERT(errno == EIO);
    return 0;
}

int test_commands_pwd_returns_current_directory(void)
{
    char expected[4096];
    struct wc_command_result result;

    WC_TEST_ASSERT(getcwd(expected, sizeof(expected)) != NULL);

    WC_TEST_ASSERT(wc_command_pwd(4095, &result) == 0);

    WC_TEST_ASSERT(result.length == strlen(expected));
    WC_TEST_ASSERT(memcmp(result.data, expected, result.length) == 0);

    wc_command_result_destroy(&result);
    return 0;
}

int test_commands_pwd_beyond_limit_returns_error(void)
{
    struct wc_command_result result;

    errno = 0;
    WC_TEST_ASSERT(wc_command_pwd(0, &result) == -1);
    WC_TEST_ASSERT(errno == EMSGSIZE);
    return 0;
}

int test_commands_cat_returns_binary_file_contents(void)
{
    char directory[] = "/tmp/wirecommand-cat-XXXXXX";
    char path[256];
    const unsigned char contents[] = {'a', 0, 'b', '\n'};
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);
    WC_TEST_ASSERT(wc_test_make_path(path, sizeof(path), directory,
                                     "content") == 0);
    WC_TEST_ASSERT(wc_test_write_file(path, contents, sizeof(contents)) == 0);

    WC_TEST_ASSERT(wc_command_cat((const unsigned char *)path, strlen(path),
                                  64, &result) == 0);

    WC_TEST_ASSERT(result.length == sizeof(contents));
    WC_TEST_ASSERT(memcmp(result.data, contents, sizeof(contents)) == 0);

    wc_command_result_destroy(&result);
    WC_TEST_ASSERT(unlink(path) == 0);
    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}

int test_commands_cat_empty_file_returns_empty_result(void)
{
    char directory[] = "/tmp/wirecommand-cat-empty-XXXXXX";
    char path[256];
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);
    WC_TEST_ASSERT(wc_test_make_path(path, sizeof(path), directory,
                                     "empty") == 0);
    WC_TEST_ASSERT(wc_test_write_file(path, "", 0) == 0);

    WC_TEST_ASSERT(wc_command_cat((const unsigned char *)path, strlen(path),
                                  0, &result) == 0);

    WC_TEST_ASSERT(result.data == NULL);
    WC_TEST_ASSERT(result.length == 0);

    wc_command_result_destroy(&result);
    WC_TEST_ASSERT(unlink(path) == 0);
    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}

int test_commands_cat_missing_file_returns_error(void)
{
    const char path[] = "/tmp/wirecommand-file-that-does-not-exist";
    struct wc_command_result result;

    errno = 0;
    WC_TEST_ASSERT(wc_command_cat((const unsigned char *)path,
                                  sizeof(path) - 1, 64, &result) == -1);
    WC_TEST_ASSERT(errno == EIO);
    return 0;
}

int test_commands_cat_directory_returns_type_error(void)
{
    char directory[] = "/tmp/wirecommand-cat-directory-XXXXXX";
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);

    errno = 0;
    WC_TEST_ASSERT(wc_command_cat((const unsigned char *)directory,
                                  strlen(directory), 64, &result) == -1);
    WC_TEST_ASSERT(errno == EIO);

    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}

int test_commands_cat_oversized_file_returns_error(void)
{
    char directory[] = "/tmp/wirecommand-cat-large-XXXXXX";
    char path[256];
    const char contents[] = "12345";
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);
    WC_TEST_ASSERT(wc_test_make_path(path, sizeof(path), directory,
                                     "large") == 0);
    WC_TEST_ASSERT(wc_test_write_file(path, contents, sizeof(contents) - 1) ==
                   0);

    errno = 0;
    WC_TEST_ASSERT(wc_command_cat((const unsigned char *)path, strlen(path),
                                  4, &result) == -1);
    WC_TEST_ASSERT(errno == EMSGSIZE);

    WC_TEST_ASSERT(unlink(path) == 0);
    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}

int test_commands_cat_unreadable_file_returns_permission_error(void)
{
    char directory[] = "/tmp/wirecommand-cat-permission-XXXXXX";
    char path[256];
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);
    WC_TEST_ASSERT(wc_test_make_path(path, sizeof(path), directory,
                                     "private") == 0);
    WC_TEST_ASSERT(wc_test_write_file(path, "x", 1) == 0);
    WC_TEST_ASSERT(chmod(path, 0000) == 0);

    if (geteuid() != 0) {
        errno = 0;
        WC_TEST_ASSERT(wc_command_cat((const unsigned char *)path, strlen(path),
                                      64, &result) == -1);
        WC_TEST_ASSERT(errno == EIO);
    }

    WC_TEST_ASSERT(chmod(path, 0600) == 0);
    WC_TEST_ASSERT(unlink(path) == 0);
    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}

int test_commands_path_with_embedded_null_is_rejected(void)
{
    const unsigned char path[] = {'/', 't', 'm', 'p', 0, 'x'};
    struct wc_command_result result;

    errno = 0;
    WC_TEST_ASSERT(wc_command_ls(path, sizeof(path), 64, &result) == -1);
    WC_TEST_ASSERT(errno == EINVAL);
    return 0;
}

int test_commands_cat_relative_path_is_rejected(void)
{
    const unsigned char path[] = "relative-file";
    struct wc_command_result result;

    errno = 0;
    WC_TEST_ASSERT(wc_command_cat(path, sizeof(path) - 1, 64, &result) == -1);
    WC_TEST_ASSERT(errno == EINVAL);
    return 0;
}

int test_commands_cat_path_with_quote_is_shell_safe(void)
{
    char directory[] = "/tmp/wirecommand-cat-quote-XXXXXX";
    char path[256];
    const char contents[] = "safe";
    struct wc_command_result result;

    WC_TEST_ASSERT(mkdtemp(directory) != NULL);
    WC_TEST_ASSERT(wc_test_make_path(path, sizeof(path), directory,
                                     "single'quote") == 0);
    WC_TEST_ASSERT(wc_test_write_file(path, contents, sizeof(contents) - 1) ==
                   0);

    WC_TEST_ASSERT(wc_command_cat((const unsigned char *)path, strlen(path),
                                  64, &result) == 0);

    WC_TEST_ASSERT(result.length == sizeof(contents) - 1);
    WC_TEST_ASSERT(memcmp(result.data, contents, result.length) == 0);

    wc_command_result_destroy(&result);
    WC_TEST_ASSERT(unlink(path) == 0);
    WC_TEST_ASSERT(rmdir(directory) == 0);
    return 0;
}
