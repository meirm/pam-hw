/*
 * test_smoke.c — smoke test for fake_usb helper
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "helpers.h"

/* Test harness macros from test_main.c */
extern void register_test(const char *name, void (*fn)(void));
#define T(name) \
    static void test_##name(void); \
    static void __attribute__((constructor)) register_##name(void) { \
        register_test(#name, test_##name); \
    } \
    static void test_##name(void)

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

T(fake_usb_creates_readable_files) {
    char tmpdir[] = "/tmp/pam_hw_test_XXXXXX";
    char path[512];
    FILE *f;
    char buf[256];
    
    /* Create temporary directory */
    EXPECT_TRUE(mkdtemp(tmpdir) != NULL);
    
    /* Create fake USB device */
    fake_usb(tmpdir, "1-1.2", "046d", "c52b", "12345678", "Test Device");
    
    /* Verify idVendor */
    snprintf(path, sizeof(path), "%s/1-1.2/idVendor", tmpdir);
    f = fopen(path, "r");
    EXPECT_TRUE(f != NULL);
    EXPECT_TRUE(fgets(buf, sizeof(buf), f) != NULL);
    buf[strcspn(buf, "\n")] = '\0';  /* strip newline */
    EXPECT_STR_EQ("046d", buf);
    fclose(f);
    
    /* Verify idProduct */
    snprintf(path, sizeof(path), "%s/1-1.2/idProduct", tmpdir);
    f = fopen(path, "r");
    EXPECT_TRUE(f != NULL);
    EXPECT_TRUE(fgets(buf, sizeof(buf), f) != NULL);
    buf[strcspn(buf, "\n")] = '\0';
    EXPECT_STR_EQ("c52b", buf);
    fclose(f);
    
    /* Verify serial */
    snprintf(path, sizeof(path), "%s/1-1.2/serial", tmpdir);
    f = fopen(path, "r");
    EXPECT_TRUE(f != NULL);
    EXPECT_TRUE(fgets(buf, sizeof(buf), f) != NULL);
    buf[strcspn(buf, "\n")] = '\0';
    EXPECT_STR_EQ("12345678", buf);
    fclose(f);
    
    /* Verify product */
    snprintf(path, sizeof(path), "%s/1-1.2/product", tmpdir);
    f = fopen(path, "r");
    EXPECT_TRUE(f != NULL);
    EXPECT_TRUE(fgets(buf, sizeof(buf), f) != NULL);
    buf[strcspn(buf, "\n")] = '\0';
    EXPECT_STR_EQ("Test Device", buf);
    fclose(f);
    
    /* Cleanup */
    snprintf(path, sizeof(path), "rm -rf %s", tmpdir);
    int ret = system(path);
    (void)ret;  /* Cleanup failure is not critical for test */
}
