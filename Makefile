CC ?= cc
CLANG_FORMAT ?= clang-format

CPPFLAGS := -Iinclude
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -O2
BUILD_DIR := build
CORE_SOURCES := src/logging.c src/buffer.c src/protocol.c src/commands.c src/queue.c src/socket_utils.c
CORE_OBJECTS := $(CORE_SOURCES:src/%.c=$(BUILD_DIR)/obj/%.o)
UDS_SOURCES := src/server_uds.c src/main_uds.c
UDS_OBJECTS := $(UDS_SOURCES:src/%.c=$(BUILD_DIR)/obj/%.o)
THREADED_UDS_SOURCES := src/server_uds_threaded.c src/main_uds_threaded.c
THREADED_UDS_OBJECTS := $(THREADED_UDS_SOURCES:src/%.c=$(BUILD_DIR)/obj/%.o)
TCP_SOURCES := src/server_tcp.c src/main_tcp.c
TCP_OBJECTS := $(TCP_SOURCES:src/%.c=$(BUILD_DIR)/obj/%.o)
CLIENT_OBJECT := $(BUILD_DIR)/obj/main_client.o
UNIT_SOURCES := tests/unit/test_main.c tests/unit/test_logging.c tests/unit/test_buffer.c tests/unit/test_protocol.c tests/unit/test_commands.c tests/unit/test_queue.c tests/unit/test_socket_utils.c
UNIT_OBJECTS := $(UNIT_SOURCES:tests/unit/%.c=$(BUILD_DIR)/obj/tests/%.o)
UNIT_RUNNER := $(BUILD_DIR)/tests/unit_tests
INTEGRATION_OBJECT := $(BUILD_DIR)/obj/tests/integration/test_uds.o
INTEGRATION_RUNNER := $(BUILD_DIR)/tests/integration_uds
TCP_INTEGRATION_OBJECT := $(BUILD_DIR)/obj/tests/integration/test_tcp.o
TCP_INTEGRATION_RUNNER := $(BUILD_DIR)/tests/integration_tcp
DEPENDENCIES := $(CORE_OBJECTS:.o=.d) $(UDS_OBJECTS:.o=.d) $(THREADED_UDS_OBJECTS:.o=.d) $(TCP_OBJECTS:.o=.d) $(CLIENT_OBJECT:.o=.d) $(UNIT_OBJECTS:.o=.d) $(INTEGRATION_OBJECT:.o=.d) $(TCP_INTEGRATION_OBJECT:.o=.d)
FORMAT_FILES := $(wildcard include/wirecommand/*.h src/*.c tests/unit/*.c tests/unit/*.h)
COVERAGE_SOURCES := $(CORE_SOURCES) $(UDS_SOURCES) $(THREADED_UDS_SOURCES) $(TCP_SOURCES) src/main_client.c

.PHONY: all test integration-test check asan ubsan coverage clean ci-clean

all: wirecommand wirecommand-uds wirecommand-uds-threaded wirecommand-tcp

$(BUILD_DIR)/obj/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj/tests/%.o: tests/unit/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/obj/tests/integration/%.o: tests/integration/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

wirecommand-uds: $(CORE_OBJECTS) $(UDS_OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@

$(THREADED_UDS_OBJECTS): CFLAGS += -pthread

wirecommand-uds-threaded: $(CORE_OBJECTS) $(THREADED_UDS_OBJECTS)
	$(CC) $(CFLAGS) -pthread $^ -o $@

$(TCP_OBJECTS): CFLAGS += -pthread

wirecommand-tcp: $(CORE_OBJECTS) $(TCP_OBJECTS)
	$(CC) $(CFLAGS) -pthread $^ -o $@

wirecommand: $(CLIENT_OBJECT) $(BUILD_DIR)/obj/buffer.o $(BUILD_DIR)/obj/protocol.o $(BUILD_DIR)/obj/socket_utils.o
	$(CC) $(CFLAGS) $^ -o $@

$(UNIT_RUNNER): $(UNIT_OBJECTS) $(CORE_OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

test: $(UNIT_RUNNER)
	$(UNIT_RUNNER)

$(INTEGRATION_RUNNER): $(INTEGRATION_OBJECT) $(BUILD_DIR)/obj/buffer.o $(BUILD_DIR)/obj/protocol.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

$(TCP_INTEGRATION_RUNNER): $(TCP_INTEGRATION_OBJECT) $(BUILD_DIR)/obj/buffer.o $(BUILD_DIR)/obj/protocol.o $(BUILD_DIR)/obj/socket_utils.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

integration-test: wirecommand-uds wirecommand-uds-threaded wirecommand-tcp $(INTEGRATION_RUNNER) $(TCP_INTEGRATION_RUNNER)
	$(INTEGRATION_RUNNER) ./wirecommand-uds
	$(INTEGRATION_RUNNER) ./wirecommand-uds-threaded threaded
	$(TCP_INTEGRATION_RUNNER) ./wirecommand-tcp ./wirecommand

check: all test integration-test

asan:
	$(MAKE) clean
	ASAN_OPTIONS=allocator_may_return_null=1:detect_leaks=1 $(MAKE) CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -O0 -g -fsanitize=address -fno-omit-frame-pointer' check

ubsan:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -O0 -g -fsanitize=undefined -fno-omit-frame-pointer' check

coverage:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -O0 -g --coverage' check
	gcov -o $(BUILD_DIR)/obj $(COVERAGE_SOURCES)

clean:
	rm -rf -- $(BUILD_DIR)
	rm -f -- *.gcov
	rm -f -- wirecommand wirecommand-uds wirecommand-uds-threaded wirecommand-tcp

ci-clean: clean
	rm -f -- build.log unit-tests.log integration-tests.log asan.log ubsan.log

-include $(DEPENDENCIES)
