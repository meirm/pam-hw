/*
 * test_usb_detect.c — Tests for USB device scanner
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "test.h"
#include "helpers.h"
#include "../src/usb_detect.h"

/* Test: Basic scan with two devices */
T(basic_two_devices) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    /* Create bus and usb directories */
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Create two fake USB devices */
    fake_usb(devices_dir, "1-1", "0a12", "0001", "0123456789", "Test Device 1");
    fake_usb(devices_dir, "2-1.2", "046d", "c52b", "ABCDEF01", "Test Device 2");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(2, count);
    
    /* Find devices by ident (order may vary) */
    int found_1 = 0, found_2 = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(devs[i].ident, "0a12:0001:0123456789") == 0) {
            found_1 = 1;
            EXPECT_STR_EQ("0a12", devs[i].vendor);
            EXPECT_STR_EQ("0001", devs[i].product);
            EXPECT_STR_EQ("0123456789", devs[i].serial);
            EXPECT_INT_EQ(1, devs[i].has_serial);
            EXPECT_STR_EQ("Test Device 1", devs[i].name);
        } else if (strcmp(devs[i].ident, "046d:c52b:ABCDEF01") == 0) {
            found_2 = 1;
            EXPECT_STR_EQ("046d", devs[i].vendor);
            EXPECT_STR_EQ("c52b", devs[i].product);
            EXPECT_STR_EQ("ABCDEF01", devs[i].serial);
            EXPECT_INT_EQ(1, devs[i].has_serial);
            EXPECT_STR_EQ("Test Device 2", devs[i].name);
        }
    }
    
    EXPECT_TRUE(found_1);
    EXPECT_TRUE(found_2);
    
    remove_tree(root);
}

/* Test: Root hub with juicy serial is skipped */
T(root_hub_skipped) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Create a root hub (starts with usb + digits) */
    fake_usb(devices_dir, "usb3", "1d6b", "0002", "0123456789", "USB Root Hub");
    
    /* Create a real device */
    fake_usb(devices_dir, "3-1", "0a12", "0001", "REALSERIAL", "Real Device");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    
    /* Only the real device should be found */
    EXPECT_STR_EQ("0a12:0001:REALSERIAL", devs[0].ident);
    
    /* Verify root hub serial is not in any device */
    for (int i = 0; i < count; i++) {
        EXPECT_TRUE(strcmp(devs[i].serial, "0123456789") != 0);
    }
    
    remove_tree(root);
}

/* Test: Device without serial file */
T(no_serial_file) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Device with no serial (NULL serial means no file created) */
    fake_usb(devices_dir, "1-1", "0a12", "0001", NULL, "No Serial Device");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("0a12:0001", devs[0].ident);
    EXPECT_INT_EQ(0, devs[0].has_serial);
    EXPECT_STR_EQ("", devs[0].serial);
    
    remove_tree(root);
}

/* Test: Garbage serial (all zeros) treated as absent */
T(garbage_serial_all_zeros) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Device with all-zero serial */
    fake_usb(devices_dir, "1-1", "0a12", "0001", "000000000", "Zero Serial Device");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("0a12:0001", devs[0].ident);
    EXPECT_INT_EQ(0, devs[0].has_serial);
    
    remove_tree(root);
}

/* Test: Empty serial treated as absent */
T(garbage_serial_empty) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Device with empty serial */
    fake_usb(devices_dir, "1-1", "0a12", "0001", "", "Empty Serial Device");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("0a12:0001", devs[0].ident);
    EXPECT_INT_EQ(0, devs[0].has_serial);
    
    remove_tree(root);
}

/* Test: xHCI-style serial with colons is kept */
T(xhci_serial_with_colons) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Device with xHCI-style serial containing colons */
    fake_usb(devices_dir, "1-1", "0a12", "0001", "0000:96:00.4", "xHCI Device");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("0a12:0001:0000:96:00.4", devs[0].ident);
    EXPECT_STR_EQ("0000:96:00.4", devs[0].serial);
    EXPECT_INT_EQ(1, devs[0].has_serial);
    
    remove_tree(root);
}

/* Test: Missing idVendor is skipped */
T(missing_idVendor_skipped) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Create device directory manually without idVendor */
    char path[512];
    snprintf(path, sizeof(path), "%s/1-1", devices_dir);
    mkdir(path, 0755);
    
    /* Only create idProduct, not idVendor */
    snprintf(path, sizeof(path), "%s/1-1/idProduct", devices_dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "0001\n");
    fclose(f);
    
    /* Also create a valid device */
    fake_usb(devices_dir, "2-1", "0a12", "0002", "VALID", "Valid Device");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("0a12:0002:VALID", devs[0].ident);
    
    remove_tree(root);
}

/* Test: Missing idProduct is skipped */
T(missing_idProduct_skipped) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Create device directory manually without idProduct */
    char path[512];
    snprintf(path, sizeof(path), "%s/1-1", devices_dir);
    mkdir(path, 0755);
    
    /* Only create idVendor, not idProduct */
    snprintf(path, sizeof(path), "%s/1-1/idVendor", devices_dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "0a12\n");
    fclose(f);
    
    /* Also create a valid device */
    fake_usb(devices_dir, "2-1", "0a12", "0002", "VALID", "Valid Device");
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("0a12:0002:VALID", devs[0].ident);
    
    remove_tree(root);
}

/* Test: Overflow safety with max=1 */
T(overflow_safe_max_1) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Create three devices */
    fake_usb(devices_dir, "1-1", "0a12", "0001", "SERIAL1", "Device 1");
    fake_usb(devices_dir, "2-1", "0a12", "0002", "SERIAL2", "Device 2");
    fake_usb(devices_dir, "3-1", "0a12", "0003", "SERIAL3", "Device 3");
    
    /* Request only 1 device */
    hw_usb_device devs[1];
    int count = 0;
    int ret = usb_scan(root, devs, 1, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    
    /* Verify one valid device was returned */
    EXPECT_TRUE(strlen(devs[0].ident) > 0);
    
    remove_tree(root);
}

/* Test: Nonexistent sys_root returns 0 devices */
T(nonexistent_sysroot) {
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan("/nonexistent/path", devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(0, count);
}

/* Test: Device without product name still works */
T(no_product_name) {
    char root[] = "/tmp/usb_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char devices_dir[256];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", root);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", root);
    mkdir(devices_dir, 0755);
    
    /* Device with no product name */
    fake_usb(devices_dir, "1-1", "0a12", "0001", "SERIAL1", NULL);
    
    hw_usb_device devs[10];
    int count = 0;
    int ret = usb_scan(root, devs, 10, &count);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("0a12:0001:SERIAL1", devs[0].ident);
    EXPECT_STR_EQ("", devs[0].name);
    
    remove_tree(root);
}
