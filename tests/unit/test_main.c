#include "test.h"

#include <stddef.h>
#include <stdio.h>

int test_logging_set_level_changes_current_level(void);
int test_logging_level_name_returns_readable_name(void);
int test_logging_invalid_level_name_returns_unknown(void);
int test_logging_disabled_record_is_not_emitted(void);
int test_logging_record_has_structured_fields(void);
int test_logging_call_preserves_errno(void);
int test_buffer_initial_state_is_empty(void);
int test_buffer_appended_bytes_are_preserved(void);
int test_buffer_non_character_value_is_preserved(void);
int test_buffer_large_append_grows_capacity(void);
int test_buffer_consumed_prefix_is_removed(void);
int test_buffer_consuming_all_bytes_makes_it_empty(void);
int test_buffer_empty_append_changes_nothing(void);
int test_buffer_append_beyond_limit_is_rejected(void);
int test_buffer_overflowing_append_is_rejected(void);
int test_buffer_consume_beyond_length_is_rejected(void);

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
        {"test_buffer_initial_state_is_empty",
         test_buffer_initial_state_is_empty},
        {"test_buffer_appended_bytes_are_preserved",
         test_buffer_appended_bytes_are_preserved},
        {"test_buffer_non_character_value_is_preserved",
         test_buffer_non_character_value_is_preserved},
        {"test_buffer_large_append_grows_capacity",
         test_buffer_large_append_grows_capacity},
        {"test_buffer_consumed_prefix_is_removed",
         test_buffer_consumed_prefix_is_removed},
        {"test_buffer_consuming_all_bytes_makes_it_empty",
         test_buffer_consuming_all_bytes_makes_it_empty},
        {"test_buffer_empty_append_changes_nothing",
         test_buffer_empty_append_changes_nothing},
        {"test_buffer_append_beyond_limit_is_rejected",
         test_buffer_append_beyond_limit_is_rejected},
        {"test_buffer_overflowing_append_is_rejected",
         test_buffer_overflowing_append_is_rejected},
        {"test_buffer_consume_beyond_length_is_rejected",
         test_buffer_consume_beyond_length_is_rejected},
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
