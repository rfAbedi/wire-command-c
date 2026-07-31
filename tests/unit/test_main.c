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
int test_protocol_request_empty_argument_is_encoded(void);
int test_protocol_request_argument_is_encoded_without_null(void);
int test_protocol_request_large_lengths_use_network_order(void);
int test_protocol_invalid_request_type_is_not_encoded(void);
int test_protocol_oversized_request_argument_is_rejected(void);
int test_protocol_request_beyond_output_limit_is_rejected(void);
int test_protocol_failed_request_preserves_existing_output(void);
int test_protocol_zero_through_five_request_bytes_need_more(void);
int test_protocol_fragmented_request_argument_needs_more(void);
int test_protocol_unknown_request_type_is_rejected(void);
int test_protocol_request_length_smaller_than_header_is_rejected(void);
int test_protocol_request_length_mismatch_is_rejected(void);
int test_protocol_complete_request_returns_argument_view(void);
int test_protocol_two_requests_parse_in_order(void);
int test_protocol_complete_request_before_partial_request_parses(void);
int test_protocol_response_examples_are_encoded(void);
int test_protocol_maximum_response_data_is_encoded(void);
int test_protocol_oversized_response_data_is_rejected(void);
int test_protocol_response_beyond_output_limit_is_rejected(void);
int test_protocol_failed_response_preserves_existing_output(void);
int test_protocol_zero_or_one_response_bytes_need_more(void);
int test_protocol_partial_response_data_needs_more(void);
int test_protocol_zero_length_response_is_complete(void);
int test_protocol_complete_response_returns_data_view(void);
int test_protocol_two_responses_parse_in_order(void);
int test_protocol_complete_response_before_partial_response_parses(void);
int test_commands_ls_lists_directory_entries(void);
int test_commands_ls_empty_directory_returns_empty_result(void);
int test_commands_ls_missing_directory_returns_error(void);
int test_commands_pwd_returns_current_directory(void);
int test_commands_pwd_beyond_limit_returns_error(void);
int test_commands_cat_returns_binary_file_contents(void);
int test_commands_cat_empty_file_returns_empty_result(void);
int test_commands_cat_missing_file_returns_error(void);
int test_commands_cat_directory_returns_type_error(void);
int test_commands_cat_oversized_file_returns_error(void);
int test_commands_cat_unreadable_file_returns_permission_error(void);
int test_commands_path_with_embedded_null_is_rejected(void);
int test_commands_cat_relative_path_is_rejected(void);
int test_commands_cat_path_with_quote_is_shell_safe(void);

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
        {"test_protocol_request_empty_argument_is_encoded",
         test_protocol_request_empty_argument_is_encoded},
        {"test_protocol_request_argument_is_encoded_without_null",
         test_protocol_request_argument_is_encoded_without_null},
        {"test_protocol_request_large_lengths_use_network_order",
         test_protocol_request_large_lengths_use_network_order},
        {"test_protocol_invalid_request_type_is_not_encoded",
         test_protocol_invalid_request_type_is_not_encoded},
        {"test_protocol_oversized_request_argument_is_rejected",
         test_protocol_oversized_request_argument_is_rejected},
        {"test_protocol_request_beyond_output_limit_is_rejected",
         test_protocol_request_beyond_output_limit_is_rejected},
        {"test_protocol_failed_request_preserves_existing_output",
         test_protocol_failed_request_preserves_existing_output},
        {"test_protocol_zero_through_five_request_bytes_need_more",
         test_protocol_zero_through_five_request_bytes_need_more},
        {"test_protocol_fragmented_request_argument_needs_more",
         test_protocol_fragmented_request_argument_needs_more},
        {"test_protocol_unknown_request_type_is_rejected",
         test_protocol_unknown_request_type_is_rejected},
        {"test_protocol_request_length_smaller_than_header_is_rejected",
         test_protocol_request_length_smaller_than_header_is_rejected},
        {"test_protocol_request_length_mismatch_is_rejected",
         test_protocol_request_length_mismatch_is_rejected},
        {"test_protocol_complete_request_returns_argument_view",
         test_protocol_complete_request_returns_argument_view},
        {"test_protocol_two_requests_parse_in_order",
         test_protocol_two_requests_parse_in_order},
        {"test_protocol_complete_request_before_partial_request_parses",
         test_protocol_complete_request_before_partial_request_parses},
        {"test_protocol_response_examples_are_encoded",
         test_protocol_response_examples_are_encoded},
        {"test_protocol_maximum_response_data_is_encoded",
         test_protocol_maximum_response_data_is_encoded},
        {"test_protocol_oversized_response_data_is_rejected",
         test_protocol_oversized_response_data_is_rejected},
        {"test_protocol_response_beyond_output_limit_is_rejected",
         test_protocol_response_beyond_output_limit_is_rejected},
        {"test_protocol_failed_response_preserves_existing_output",
         test_protocol_failed_response_preserves_existing_output},
        {"test_protocol_zero_or_one_response_bytes_need_more",
         test_protocol_zero_or_one_response_bytes_need_more},
        {"test_protocol_partial_response_data_needs_more",
         test_protocol_partial_response_data_needs_more},
        {"test_protocol_zero_length_response_is_complete",
         test_protocol_zero_length_response_is_complete},
        {"test_protocol_complete_response_returns_data_view",
         test_protocol_complete_response_returns_data_view},
        {"test_protocol_two_responses_parse_in_order",
         test_protocol_two_responses_parse_in_order},
        {"test_protocol_complete_response_before_partial_response_parses",
         test_protocol_complete_response_before_partial_response_parses},
        {"test_commands_ls_lists_directory_entries",
         test_commands_ls_lists_directory_entries},
        {"test_commands_ls_empty_directory_returns_empty_result",
         test_commands_ls_empty_directory_returns_empty_result},
        {"test_commands_ls_missing_directory_returns_error",
         test_commands_ls_missing_directory_returns_error},
        {"test_commands_pwd_returns_current_directory",
         test_commands_pwd_returns_current_directory},
        {"test_commands_pwd_beyond_limit_returns_error",
         test_commands_pwd_beyond_limit_returns_error},
        {"test_commands_cat_returns_binary_file_contents",
         test_commands_cat_returns_binary_file_contents},
        {"test_commands_cat_empty_file_returns_empty_result",
         test_commands_cat_empty_file_returns_empty_result},
        {"test_commands_cat_missing_file_returns_error",
         test_commands_cat_missing_file_returns_error},
        {"test_commands_cat_directory_returns_type_error",
         test_commands_cat_directory_returns_type_error},
        {"test_commands_cat_oversized_file_returns_error",
         test_commands_cat_oversized_file_returns_error},
        {"test_commands_cat_unreadable_file_returns_permission_error",
         test_commands_cat_unreadable_file_returns_permission_error},
        {"test_commands_path_with_embedded_null_is_rejected",
         test_commands_path_with_embedded_null_is_rejected},
        {"test_commands_cat_relative_path_is_rejected",
         test_commands_cat_relative_path_is_rejected},
        {"test_commands_cat_path_with_quote_is_shell_safe",
         test_commands_cat_path_with_quote_is_shell_safe},
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
