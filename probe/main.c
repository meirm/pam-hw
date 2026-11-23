/*
 * probe/main.c — USB device probe utility for pam-hw
 * 
 * Lists connected USB devices with enrollment annotations.
 * Interfaces: consumes usb_scan, config_load, identities_load.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include "../src/usb_detect.h"
#include "../src/config.h"
#include "../src/identities.h"

static void usage(FILE *out) {
    fprintf(out, "Usage: pam-hw-probe list [options]\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --mine             Show only devices enrolled by current user\n");
    fprintf(out, "  --conf PATH        Configuration file (default: /etc/pamhw/pamhw.conf)\n");
    fprintf(out, "  --sys-root DIR     Override sysfs root directory\n");
    fprintf(out, "  --identities PATH  Override identities file (default: /etc/pamhw/identities)\n");
}

int main(int argc, char **argv) {
    const char *conf_path = "/etc/pamhw/pamhw.conf";
    const char *sys_root_override = NULL;
    const char *identities_path = "/etc/pamhw/identities";
    int mine_flag = 0;
    int i;
    
    /* Parse arguments */
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    
    if (strcmp(argv[1], "list") != 0) {
        usage(stderr);
        return 2;
    }
    
    /* Parse flags */
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--mine") == 0) {
            mine_flag = 1;
        } else if (strcmp(argv[i], "--conf") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --conf requires an argument\n");
                usage(stderr);
                return 2;
            }
            conf_path = argv[i];
        } else if (strcmp(argv[i], "--sys-root") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --sys-root requires an argument\n");
                usage(stderr);
                return 2;
            }
            sys_root_override = argv[i];
        } else if (strcmp(argv[i], "--identities") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --identities requires an argument\n");
                usage(stderr);
                return 2;
            }
            identities_path = argv[i];
        } else {
            fprintf(stderr, "Error: unknown option: %s\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    
    /* Load configuration */
    hw_config cfg;
    if (config_load(conf_path, &cfg) != 0) {
        fprintf(stderr, "Error: failed to load configuration\n");
        return 1;
    }
    
    /* Apply sys_root override if provided */
    const char *sys_root = sys_root_override ? sys_root_override : cfg.sys_root;
    
    /* Load identities */
    hw_identity identities[256];
    int identity_count = 0;
    if (identities_load(identities_path, identities, 256, &identity_count) != 0) {
        fprintf(stderr, "Error: failed to load identities\n");
        return 1;
    }
    
    /* Get current username for --mine */
    char username[64] = {0};
    if (mine_flag) {
        uid_t uid = getuid();
        struct passwd *pw = getpwuid(uid);
        if (!pw) {
            fprintf(stderr, "Error: could not determine current user\n");
            return 1;
        }
        snprintf(username, sizeof(username), "%s", pw->pw_name);
    }
    
    /* Scan USB devices */
    hw_usb_device devices[128];
    int device_count = 0;
    if (usb_scan(sys_root, devices, 128, &device_count) != 0) {
        /* Scan errors are not fatal - just print nothing */
        return 0;
    }
    
    /* Print results */
    for (i = 0; i < device_count; i++) {
        const hw_usb_device *dev = &devices[i];
        
        /* Find if this device is enrolled */
        const hw_identity *enrolled = identities_find_device(
            identities, identity_count, HW_USB, dev->ident);
        
        /* If --mine is set, only show devices enrolled by current user */
        if (mine_flag) {
            if (!enrolled || strcmp(enrolled->user, username) != 0) {
                continue;
            }
            /* Print identity line format for --mine */
            printf("usb\t%s\t%s", dev->ident, username);
            if (enrolled->verify[0]) {
                printf("\tverify=%s", enrolled->verify);
            }
            if (enrolled->mode[0]) {
                printf("\tmode=%s", enrolled->mode);
            }
            printf("\n");
        } else {
            /* Normal list mode: usb <ident> # comments */
            printf("usb\t%s", dev->ident);
            
            /* Add product name comment if available */
            if (dev->name[0]) {
                printf("\t# %s", dev->name);
            }
            
            /* Add enrollment annotation if enrolled */
            if (enrolled) {
                /* Use separate comment segment for greppability */
                if (dev->name[0]) {
                    printf("  # enrolled: %s", enrolled->user);
                } else {
                    printf("\t# enrolled: %s", enrolled->user);
                }
            }
            
            printf("\n");
        }
    }
    
    return 0;
}
