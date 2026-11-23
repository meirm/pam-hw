/*
 * test_main.c — minimal test harness for pam-hw
 */
#include <stdio.h>
#include "test.h"

/* Test registry */
typedef void (*test_fn)(void);
typedef struct {
    const char *name;
    test_fn fn;
} test_case;

static test_case tests[256];
static int test_count = 0;
static int tests_run = 0;
static int tests_failed = 0;

/* Register a test */
void register_test(const char *name, test_fn fn) {
    tests[test_count].name = name;
    tests[test_count].fn = fn;
    test_count++;
}

/* Run all tests */
int main(void) {
    printf("Running %d tests...\n", test_count);
    
    for (int i = 0; i < test_count; i++) {
        tests_run++;
        tests[i].fn();
        printf("  ✓ %s\n", tests[i].name);
    }
    
    printf("\n%d/%d tests passed.\n", tests_run - tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
