/*
 * test_main.c — minimal test harness for pam-hw
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Assertion macros */
#define EXPECT_INT_EQ(expected, actual) do { \
    int _exp = (expected); \
    int _act = (actual); \
    if (_exp != _act) { \
        fprintf(stderr, "FAIL: %s:%d: Expected %d, got %d\n", \
                __FILE__, __LINE__, _exp, _act); \
        exit(1); \
    } \
} while (0)

#define EXPECT_STR_EQ(expected, actual) do { \
    const char *_exp = (expected); \
    const char *_act = (actual); \
    if (strcmp(_exp, _act) != 0) { \
        fprintf(stderr, "FAIL: %s:%d: Expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, _exp, _act); \
        exit(1); \
    } \
} while (0)

#define EXPECT_TRUE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: Expected true: %s\n", \
                __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

/* Test registration macro */
#define T(name) \
    static void test_##name(void); \
    static void __attribute__((constructor)) register_##name(void) { \
        register_test(#name, test_##name); \
    } \
    static void test_##name(void)

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
