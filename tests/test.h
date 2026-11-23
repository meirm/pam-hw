/*
 * test.h — shared test infrastructure for pam-hw tests
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test registration function (implemented in test_main.c) */
extern void register_test(const char *name, void (*fn)(void));

/* Test registration macro */
#define T(name) \
    static void test_##name(void); \
    static void __attribute__((constructor)) register_##name(void) { \
        register_test(#name, test_##name); \
    } \
    static void test_##name(void)

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

#endif /* TEST_H */
