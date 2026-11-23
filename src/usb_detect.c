/*
 * usb_detect.c — Hardware USB device scanner implementation
 */
#define _DEFAULT_SOURCE
#include "usb_detect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

/* Helper: copy string safely with truncation */
static void safe_strcpy(char *dest, const char *src, size_t size) {
    if (size == 0) return;
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

/* Helper: trim trailing whitespace (newlines) in place */
static void trim(char *s) {
    char *p = s + strlen(s);
    while (p > s && isspace((unsigned char)p[-1])) p--;
    *p = '\0';
}

/* Helper: check if basename starts with "usb" followed by digits (root hub) */
static int is_root_hub(const char *name) {
    if (strncmp(name, "usb", 3) != 0) return 0;
    
    const char *p = name + 3;
    if (!*p || !isdigit((unsigned char)*p)) return 0;
    
    /* All remaining chars must be digits */
    while (*p) {
        if (!isdigit((unsigned char)*p)) return 0;
        p++;
    }
    
    return 1;
}

/* Helper: check if serial is garbage (empty, all zeros, or has non-printable) */
static int is_garbage_serial(const char *serial) {
    if (!serial || !*serial) return 1;
    
    int all_zeros = 1;
    
    for (const char *p = serial; *p; p++) {
        /* Check for non-printable characters */
        if (!isprint((unsigned char)*p)) return 1;
        
        /* Check if it's not a zero digit */
        if (*p != '0') all_zeros = 0;
    }
    
    /* All-zero serial is garbage */
    return all_zeros;
}

/* Helper: read a sysfs file into buffer */
static int read_sysfs_file(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    if (fgets(buf, size, f) == NULL) {
        fclose(f);
        return -1;
    }
    
    fclose(f);
    trim(buf);
    return 0;
}

/* Helper: convert to lowercase */
static void to_lowercase(char *s) {
    for (char *p = s; *p; p++) {
        *p = tolower((unsigned char)*p);
    }
}

/*
 * usb_scan: Scan sysfs for USB devices
 */
int usb_scan(const char *sys_root, hw_usb_device *out, int max, int *count) {
    char devices_path[512];
    DIR *dir;
    struct dirent *entry;
    
    *count = 0;
    
    /* Build path to USB devices directory */
    snprintf(devices_path, sizeof(devices_path), "%s/bus/usb/devices", sys_root);
    
    /* Open devices directory */
    dir = opendir(devices_path);
    if (!dir) {
        /* Not an error if sysfs doesn't exist */
        return 0;
    }
    
    /* Scan all entries */
    while ((entry = readdir(dir)) != NULL && *count < max) {
        char dev_path[512];
        char buf[256];
        hw_usb_device dev;
        
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        /* Skip root hubs (usb1, usb2, etc.) */
        if (is_root_hub(entry->d_name)) {
            continue;
        }
        
        /* Initialize device structure */
        memset(&dev, 0, sizeof(dev));
        
        /* Read idVendor */
        snprintf(dev_path, sizeof(dev_path), "%s/%s/idVendor", devices_path, entry->d_name);
        if (read_sysfs_file(dev_path, buf, sizeof(buf)) != 0) {
            continue;  /* Skip if idVendor missing */
        }
        safe_strcpy(dev.vendor, buf, sizeof(dev.vendor));
        to_lowercase(dev.vendor);
        
        /* Read idProduct */
        snprintf(dev_path, sizeof(dev_path), "%s/%s/idProduct", devices_path, entry->d_name);
        if (read_sysfs_file(dev_path, buf, sizeof(buf)) != 0) {
            continue;  /* Skip if idProduct missing */
        }
        safe_strcpy(dev.product, buf, sizeof(dev.product));
        to_lowercase(dev.product);
        
        /* Read serial (optional) */
        snprintf(dev_path, sizeof(dev_path), "%s/%s/serial", devices_path, entry->d_name);
        if (read_sysfs_file(dev_path, buf, sizeof(buf)) == 0 && !is_garbage_serial(buf)) {
            safe_strcpy(dev.serial, buf, sizeof(dev.serial));
            dev.has_serial = 1;
        } else {
            dev.serial[0] = '\0';
            dev.has_serial = 0;
        }
        
        /* Read product name (optional) */
        snprintf(dev_path, sizeof(dev_path), "%s/%s/product", devices_path, entry->d_name);
        if (read_sysfs_file(dev_path, buf, sizeof(buf)) == 0) {
            safe_strcpy(dev.name, buf, sizeof(dev.name));
        } else {
            dev.name[0] = '\0';
        }
        
        /* Build identifier: vid:pid or vid:pid:serial */
        if (dev.has_serial) {
            snprintf(dev.ident, sizeof(dev.ident), "%s:%s:%s", 
                     dev.vendor, dev.product, dev.serial);
        } else {
            snprintf(dev.ident, sizeof(dev.ident), "%s:%s", 
                     dev.vendor, dev.product);
        }
        
        /* Add to output array */
        out[*count] = dev;
        (*count)++;
    }
    
    closedir(dir);
    return 0;
}
