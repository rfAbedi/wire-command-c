#include "test.h"

#include <stddef.h>
#include <stdio.h>

int test_logging_set_level_changes_current_level(void);
int test_logging_level_name_returns_readable_name(void);
int test_logging_invalid_level_name_returns_unknown(void);
int test_logging_disabled_record_is_not_emitted(void);
int test_logging_record_has_structured_fields(void);
int test_logging_call_preserves_errno(void);

int main(void)
{
    static const struct wc_test_case tests[] = {
        {"test_logging_set_level_changes_current_level",
         test_logging_set_level_changes_current_level},
        {"test_logging_level_name_returns_readable_name",
         test_logging_level_name_returns_readable_name},
        {"test_logging_invalid_level_name_returns_unknown",
         test_logging_invalid_level_name_returns_unknown},
        {"test_logging_disabled_record_is_not_emitted",
         test_logging_disabled_record_is_not_emitted},
        {"test_logging_record_has_structured_fields",
         test_logging_record_has_structured_fields},
        {"test_logging_call_preserves_errno",
         test_logging_call_preserves_errno},
    };
    size_t index;
    size_t failures = 0;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int result = tests[index].function();

        if (result == 0) {
            (void)printf("ok %zu - %s\n", index + 1, tests[index].name);
        } else {
            (void)printf("not ok %zu - %s\n", index + 1,
                         tests[index].name);
            ++failures;
        }
    }

    (void)printf("1..%zu\n", sizeof(tests) / sizeof(tests[0]));
    return failures == 0 ? 0 : 1;
}
