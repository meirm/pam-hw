/*
 * test_smoke.c — smoke test for fake_usb helper
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"
#include "helpers.h"

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
    remove_tree(tmpdir);
}
