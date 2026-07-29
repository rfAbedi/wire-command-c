CC ?= cc
CLANG_FORMAT ?= clang-format

CPPFLAGS := -Iinclude
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -O2
BUILD_DIR := build
CORE_SOURCES := src/logging.c
CORE_OBJECTS := $(CORE_SOURCES:src/%.c=$(BUILD_DIR)/obj/%.o)
UNIT_SOURCES := tests/unit/test_main.c tests/unit/test_logging.c
UNIT_OBJECTS := $(UNIT_SOURCES:tests/unit/%.c=$(BUILD_DIR)/obj/tests/%.o)
UNIT_RUNNER := $(BUILD_DIR)/tests/unit_tests
DEPENDENCIES := $(CORE_OBJECTS:.o=.d) $(UNIT_OBJECTS:.o=.d)
FORMAT_FILES := $(wildcard include/wirecommand/*.h src/*.c tests/unit/*.c tests/unit/*.h)

.PHONY: all test coverage clean

all: $(CORE_OBJECTS)

$(BUILD_DIR)/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj/tests/%.o: tests/unit/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(UNIT_RUNNER): $(UNIT_OBJECTS) $(CORE_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

test: $(UNIT_RUNNER)
	$(UNIT_RUNNER)

coverage:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -O0 -g --coverage' test
	gcov -o $(BUILD_DIR)/obj src/logging.c

clean:
	rm -rf -- $(BUILD_DIR)
	rm -f -- *.gcov

-include $(DEPENDENCIES)
