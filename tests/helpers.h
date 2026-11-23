/*
 * helpers.h — test helpers for pam-hw
 */
#ifndef HELPERS_H
#define HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/*
 * fake_usb: Create a fake sysfs USB device tree
 * 
 * Arguments:
 *   root:    Base directory for the fake sysfs tree
 *   devid:   Device identifier (e.g., "1-1.2")
 *   vid:     Vendor ID (e.g., "046d")
 *   pid:     Product ID (e.g., "c52b")
 *   serial:  Serial number string (can be NULL)
 *   product: Product name string (can be NULL)
 *
 * Creates: root/devid/{idVendor, idProduct, serial, product}
 */
static inline void fake_usb(const char *root, const char *devid,
                           const char *vid, const char *pid,
                           const char *serial, const char *product) {
    char path[512];
    FILE *f;
    
    /* Create device directory */
    snprintf(path, sizeof(path), "%s/%s", root, devid);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
        exit(1);
    }
    
    /* Write idVendor */
    snprintf(path, sizeof(path), "%s/%s/idVendor", root, devid);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
        exit(1);
    }
    fprintf(f, "%s\n", vid);
    fclose(f);
    
    /* Write idProduct */
    snprintf(path, sizeof(path), "%s/%s/idProduct", root, devid);
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
        exit(1);
    }
    fprintf(f, "%s\n", pid);
    fclose(f);
    
    /* Write serial if provided */
    if (serial) {
        snprintf(path, sizeof(path), "%s/%s/serial", root, devid);
        f = fopen(path, "w");
        if (!f) {
            fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
            exit(1);
        }
        fprintf(f, "%s\n", serial);
        fclose(f);
    }
    
    /* Write product if provided */
    if (product) {
        snprintf(path, sizeof(path), "%s/%s/product", root, devid);
        f = fopen(path, "w");
        if (!f) {
            fprintf(stderr, "Failed to create %s: %s\n", path, strerror(errno));
            exit(1);
        }
        fprintf(f, "%s\n", product);
        fclose(f);
    }
}

#endif /* HELPERS_H */
