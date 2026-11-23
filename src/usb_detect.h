/*
 * usb_detect.h — Hardware USB device scanner
 */
#ifndef USB_DETECT_H
#define USB_DETECT_H

typedef struct {
    char ident[128];      /* vid:pid or vid:pid:serial */
    char vendor[8];       /* Vendor ID (hex) */
    char product[8];      /* Product ID (hex) */
    char serial[128];     /* Serial number */
    int has_serial;       /* 1 if valid serial, 0 if absent/garbage */
    char name[64];        /* Product name */
} hw_usb_device;

/*
 * usb_scan: Scan sysfs for USB devices
 * 
 * Arguments:
 *   sys_root: Root of sysfs tree (e.g., "/sys")
 *   out:      Output array for devices
 *   max:      Maximum number of devices to return
 *   count:    Output parameter for number of devices found
 * 
 * Returns: 0 on success, non-zero on error
 */
int usb_scan(const char *sys_root, hw_usb_device *out, int max, int *count);

#endif /* USB_DETECT_H */
