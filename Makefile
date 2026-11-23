# Makefile for pam-hw — USB hardware presence PAM module

CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2
LDFLAGS = -lpam

# Install paths
PREFIX = /usr/local
PAMDIR = /lib64/security
DESTDIR =

# Targets
MODULE = pam_hw.so
PROBE = pam-hw-probe
PROBE_TEST = pam-hw-probe-test
TEST_BIN = tests/run_tests

# Source files
MODULE_SRC = src/pam_hw.c
CORE_SRC = src/config.c src/identities.c src/usb_detect.c src/evaluate.c
PROBE_SRC = probe/main.c
TEST_SRC = tests/test_main.c tests/test_smoke.c tests/test_identities.c tests/test_config.c tests/test_usb_detect.c tests/test_evaluate.c tests/test_probe.c tests/test_symbols.c

.PHONY: all test install uninstall clean

all: $(MODULE) $(PROBE) $(TEST_BIN)

# PAM module (shared library)
$(MODULE): $(MODULE_SRC) $(CORE_SRC)
	$(CC) $(CFLAGS) -fvisibility=hidden -fPIC -shared -o $@ $(MODULE_SRC) $(CORE_SRC) $(LDFLAGS)

# Probe utility
$(PROBE): $(PROBE_SRC) src/config.c src/identities.c src/usb_detect.c
	$(CC) $(CFLAGS) -fvisibility=hidden -o $@ $^

# Test-instrumented probe (NOT built by default, NOT installed)
$(PROBE_TEST): $(PROBE_SRC) src/config.c src/identities.c src/usb_detect.c
	$(CC) $(CFLAGS) -fvisibility=hidden -DPAMHW_TEST -o $@ $^

# Test binary
$(TEST_BIN): $(TEST_SRC) src/identities.c src/config.c src/usb_detect.c src/evaluate.c
	$(CC) $(CFLAGS) -I tests -o $@ $(TEST_SRC) src/identities.c src/config.c src/usb_detect.c src/evaluate.c -ldl

# Run tests
test: $(TEST_BIN) $(MODULE) $(PROBE) $(PROBE_TEST)
	./$(TEST_BIN)

# Install
install: $(MODULE) $(PROBE)
	install -d -m 0700 $(DESTDIR)/etc/pamhw
	install -d $(DESTDIR)$(PAMDIR)
	install -m 0644 $(MODULE) $(DESTDIR)$(PAMDIR)/$(MODULE)
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -m 0755 $(PROBE) $(DESTDIR)$(PREFIX)/sbin/pamhw-probe

# Uninstall
uninstall:
	rm -f $(DESTDIR)$(PAMDIR)/$(MODULE)
	rm -f $(DESTDIR)$(PREFIX)/sbin/pamhw-probe

# Clean build artifacts
clean:
	rm -f $(MODULE) $(PROBE) $(PROBE_TEST) $(TEST_BIN)
