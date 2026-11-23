# Makefile for pam-hw — USB hardware presence PAM module

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2
LDFLAGS = -lpam

# Targets
MODULE = pam_hw.so
PROBE = pam-hw-probe
PROBE_TEST = pam-hw-probe-test
TEST_BIN = tests/run_tests

# Source files
MODULE_SRC = src/pam_hw.c
CORE_SRC = src/config.c src/identities.c src/usb_detect.c src/evaluate.c
PROBE_SRC = probe/main.c
TEST_SRC = tests/test_main.c tests/test_smoke.c tests/test_identities.c tests/test_config.c tests/test_usb_detect.c tests/test_evaluate.c tests/test_probe.c

.PHONY: all test install uninstall clean

all: $(MODULE) $(PROBE) $(TEST_BIN)

# PAM module (shared library)
$(MODULE): $(MODULE_SRC) $(CORE_SRC)
	$(CC) $(CFLAGS) -fPIC -shared -o $@ $(MODULE_SRC) $(CORE_SRC) $(LDFLAGS)

# Probe utility
$(PROBE): $(PROBE_SRC) src/config.c src/identities.c src/usb_detect.c
	$(CC) $(CFLAGS) -o $@ $^

# Test-instrumented probe (NOT built by default, NOT installed)
$(PROBE_TEST): $(PROBE_SRC) src/config.c src/identities.c src/usb_detect.c
	$(CC) $(CFLAGS) -DPAMHW_TEST -o $@ $^

# Test binary
$(TEST_BIN): $(TEST_SRC) src/identities.c src/config.c src/usb_detect.c src/evaluate.c
	$(CC) $(CFLAGS) -I tests -o $@ $(TEST_SRC) src/identities.c src/config.c src/usb_detect.c src/evaluate.c

# Run tests
test: $(TEST_BIN) $(PROBE) $(PROBE_TEST)
	./$(TEST_BIN)

# Install/uninstall stubs (finalized in Task 9)
install:
	@echo "Install target: stub (will be implemented in Task 9)"

uninstall:
	@echo "Uninstall target: stub (will be implemented in Task 9)"

# Clean build artifacts
clean:
	rm -f $(MODULE) $(PROBE) $(PROBE_TEST) $(TEST_BIN)
