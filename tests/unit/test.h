#ifndef WIRECOMMAND_TEST_H
#define WIRECOMMAND_TEST_H

#include <stdio.h>
#include <string.h>

struct wc_test_case {
    const char *name;
    int (*function)(void);
};

#define WC_TEST_ASSERT(condition)                                             \
    do {                                                                      \
        if (!(condition)) {                                                   \
            (void)fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__,  \
                          __LINE__, #condition);                              \
            return 1;                                                         \
        }                                                                     \
    } while (0)

#define WC_TEST_ASSERT_STRING_EQUAL(expected, actual) \
    WC_TEST_ASSERT(strcmp((expected), (actual)) == 0)

#endif
