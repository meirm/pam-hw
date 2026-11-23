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
#include <syslog.h>
#include <errno.h>
#include <ctype.h>
#include "../src/usb_detect.h"
#include "../src/config.h"
#include "../src/identities.h"

/* Test instrumentation: allow env override of uid functions */
#ifdef PAMHW_TEST
static uid_t test_geteuid(void) {
    const char *env = getenv("PAMHW_TEST_EUID");
    if (env && *env) {
        char *endptr;
        long parsed = strtol(env, &endptr, 10);
        if (*endptr == '\0' && parsed >= 0 && parsed <= 65535) {
            return (uid_t)parsed;
        }
    }
    return geteuid();
}

static uid_t test_getuid(void) {
    const char *env = getenv("PAMHW_TEST_UID");
    if (env && *env) {
        char *endptr;
        long parsed = strtol(env, &endptr, 10);
        if (*endptr == '\0' && parsed >= 0 && parsed <= 65535) {
            return (uid_t)parsed;
        }
    }
    return getuid();
}
#else
#define test_geteuid() geteuid()
#define test_getuid() getuid()
#endif

static void usage(FILE *out) {
    fprintf(out, "Usage: pam-hw-probe <command> [options]\n");
    fprintf(out, "Commands:\n");
    fprintf(out, "  list [options]                List USB devices\n");
    fprintf(out, "  add <ident|AUTO> [options]    Enroll device\n");
    fprintf(out, "  remove <ident|all>            Remove enrollment\n");
    fprintf(out, "\nOptions:\n");
    fprintf(out, "  --mine                        Show only devices enrolled by current user\n");
    fprintf(out, "  --user NAME                   Target username (root only)\n");
    fprintf(out, "  --verify presence|secret|sign Verification method (default: presence)\n");
    fprintf(out, "  --conf PATH                   Configuration file\n");
    fprintf(out, "  --sys-root DIR                Override sysfs root\n");
    fprintf(out, "  --identities PATH             Override identities file\n");
}

/* Helper: get invoker username from uid */
static int get_username(uid_t uid, char *buf, size_t size) {
    struct passwd *pw = getpwuid(uid);
    if (!pw) return -1;
    snprintf(buf, size, "%s", pw->pw_name);
    return 0;
}

/* Helper: resolve invoker identity (SUDO_UID if set, else real uid) */
static int resolve_invoker(char *username, size_t size, int *is_root) {
    uid_t uid;
    const char *sudo_uid_str = getenv("SUDO_UID");
    
    /* Root status is ONLY based on real uid, not SUDO_UID */
    *is_root = (test_getuid() == 0);
    
    /* Invoker username comes from SUDO_UID if present, else real uid */
    if (sudo_uid_str && *sudo_uid_str) {
        char *endptr;
        long parsed = strtol(sudo_uid_str, &endptr, 10);
        if (*endptr == '\0' && parsed >= 0 && parsed <= 65535) {
            uid = (uid_t)parsed;
        } else {
            uid = test_getuid();
        }
    } else {
        uid = test_getuid();
    }
    
    return get_username(uid, username, size);
}

/* Command: list */
static int cmd_list(int argc, char **argv) {
    const char *conf_path = "/etc/pamhw/pamhw.conf";
    const char *sys_root_override = NULL;
    const char *identities_path = "/etc/pamhw/identities";
    int mine_flag = 0;
    int i;
    
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
        uid_t uid = test_getuid();
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

/* Command: add */
static int cmd_add(int argc, char **argv) {
    if (test_geteuid() != 0) {
        fprintf(stderr, "Error: enrollment changes require sudo\n");
        return 1;
    }
    
    if (argc < 3) {
        fprintf(stderr, "Error: add requires an identifier\n");
        usage(stderr);
        return 2;
    }
    
    const char *ident_arg = argv[2];
    const char *conf_path = "/etc/pamhw/pamhw.conf";
    const char *sys_root_override = NULL;
    const char *identities_path = "/etc/pamhw/identities";
    const char *target_user = NULL;
    const char *verify = "presence";
    int i;
    
    /* Parse options */
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--user") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --user requires an argument\n");
                return 2;
            }
            target_user = argv[i];
        } else if (strcmp(argv[i], "--verify") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --verify requires an argument\n");
                return 2;
            }
            verify = argv[i];
        } else if (strcmp(argv[i], "--conf") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --conf requires an argument\n");
                return 2;
            }
            conf_path = argv[i];
        } else if (strcmp(argv[i], "--sys-root") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --sys-root requires an argument\n");
                return 2;
            }
            sys_root_override = argv[i];
        } else if (strcmp(argv[i], "--identities") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --identities requires an argument\n");
                return 2;
            }
            identities_path = argv[i];
        } else {
            fprintf(stderr, "Error: unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    
    /* R1 tier limit: only presence accepted */
    if (strcmp(verify, "presence") != 0) {
        fprintf(stderr, "Error: verification method '%s' arrives in R3/R4\n", verify);
        return 1;
    }
    
    /* Resolve invoker identity */
    char invoker[64];
    int is_root;
    if (resolve_invoker(invoker, sizeof(invoker), &is_root) != 0) {
        fprintf(stderr, "Error: could not determine invoker identity\n");
        return 1;
    }
    
    /* Determine target user */
    char user[64];
    if (target_user) {
        /* --user specified */
        if (!is_root && strcmp(target_user, invoker) != 0) {
            fprintf(stderr, "Error: non-root users can only enroll their own devices\n");
            return 1;
        }
        snprintf(user, sizeof(user), "%s", target_user);
    } else {
        /* Default to invoker */
        snprintf(user, sizeof(user), "%s", invoker);
    }
    
    /* Load config */
    hw_config cfg;
    if (config_load(conf_path, &cfg) != 0) {
        fprintf(stderr, "Error: failed to load configuration\n");
        return 1;
    }
    
    const char *sys_root = sys_root_override ? sys_root_override : cfg.sys_root;
    
    /* Load existing identities */
    hw_identity identities[256];
    int identity_count = 0;
    identities_load(identities_path, identities, 256, &identity_count);
    
    /* Resolve identifier */
    char ident[128];
    if (strcmp(ident_arg, "AUTO") == 0) {
        /* AUTO: scan for exactly one unenrolled device */
        hw_usb_device devices[128];
        int device_count = 0;
        if (usb_scan(sys_root, devices, 128, &device_count) != 0) {
            fprintf(stderr, "Error: failed to scan USB devices\n");
            return 1;
        }
        
        /* Filter to unenrolled devices */
        int unenrolled_idx = -1;
        int unenrolled_count = 0;
        for (i = 0; i < device_count; i++) {
            const hw_identity *enrolled = identities_find_device(
                identities, identity_count, HW_USB, devices[i].ident);
            if (!enrolled) {
                unenrolled_idx = i;
                unenrolled_count++;
            }
        }
        
        if (unenrolled_count == 0) {
            fprintf(stderr, "Error: AUTO requires at least one unenrolled device\n");
            fprintf(stderr, "Connected devices:\n");
            for (i = 0; i < device_count; i++) {
                const hw_identity *enrolled = identities_find_device(
                    identities, identity_count, HW_USB, devices[i].ident);
                fprintf(stderr, "  usb %s", devices[i].ident);
                if (enrolled) {
                    fprintf(stderr, " (enrolled: %s)", enrolled->user);
                }
                fprintf(stderr, "\n");
            }
            return 1;
        } else if (unenrolled_count > 1) {
            fprintf(stderr, "Error: AUTO requires exactly one unenrolled device, found %d:\n", unenrolled_count);
            for (i = 0; i < device_count; i++) {
                const hw_identity *enrolled = identities_find_device(
                    identities, identity_count, HW_USB, devices[i].ident);
                if (!enrolled) {
                    fprintf(stderr, "  usb %s", devices[i].ident);
                    if (devices[i].name[0]) {
                        fprintf(stderr, " # %s", devices[i].name);
                    }
                    fprintf(stderr, "\n");
                }
            }
            return 1;
        }
        
        /* Exactly one unenrolled device */
        snprintf(ident, sizeof(ident), "%s", devices[unenrolled_idx].ident);
    } else {
        /* Normalize explicit identifier */
        if (identities_normalize(HW_USB, ident_arg, ident, sizeof(ident)) != 0) {
            fprintf(stderr, "Error: invalid identifier format: %s\n", ident_arg);
            return 2;
        }
    }
    
    /* Build identity record */
    hw_identity new_identity = {0};
    new_identity.type = HW_USB;
    snprintf(new_identity.ident, sizeof(new_identity.ident), "%s", ident);
    snprintf(new_identity.user, sizeof(new_identity.user), "%s", user);
    snprintf(new_identity.verify, sizeof(new_identity.verify), "%s", verify);
    
    /* Add identity */
    int result = identities_add(identities, &identity_count, 256, &new_identity, invoker, is_root);
    if (result == -1) {
        /* Hijack attempt */
        const hw_identity *existing = identities_find_device(identities, identity_count, HW_USB, ident);
        if (existing) {
            fprintf(stderr, "Error: device %s is already enrolled by user %s\n", ident, existing->user);
        } else {
            fprintf(stderr, "Error: could not add identity\n");
        }
        return 1;
    } else if (result == -2) {
        fprintf(stderr, "Error: non-root users can only manage their own devices\n");
        return 1;
    }
    
    /* Save identities */
    if (identities_save(identities_path, identities, identity_count) != 0) {
        fprintf(stderr, "Error: failed to save identities file\n");
        return 1;
    }
    
    /* Audit log */
    openlog("pamhw-probe", LOG_PID, LOG_AUTHPRIV);
    syslog(LOG_NOTICE, "add %s -> %s by %s", ident, user, invoker);
    closelog();
    
    return 0;
}

/* Command: remove */
static int cmd_remove(int argc, char **argv) {
    if (test_geteuid() != 0) {
        fprintf(stderr, "Error: enrollment changes require sudo\n");
        return 1;
    }
    
    if (argc < 3) {
        fprintf(stderr, "Error: remove requires an identifier or 'all'\n");
        usage(stderr);
        return 2;
    }
    
    const char *ident_arg = argv[2];
    const char *identities_path = "/etc/pamhw/identities";
    int i;
    
    /* Parse options */
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--identities") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --identities requires an argument\n");
                return 2;
            }
            identities_path = argv[i];
        } else {
            fprintf(stderr, "Error: unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    
    /* Resolve invoker identity */
    char invoker[64];
    int is_root;
    if (resolve_invoker(invoker, sizeof(invoker), &is_root) != 0) {
        fprintf(stderr, "Error: could not determine invoker identity\n");
        return 1;
    }
    
    /* Load existing identities */
    hw_identity identities[256];
    int identity_count = 0;
    if (identities_load(identities_path, identities, 256, &identity_count) != 0) {
        /* Missing file is OK for remove */
        return 0;
    }
    
    /* Remove identity(-ies) */
    int removed = identities_remove(identities, identity_count, ident_arg, invoker, is_root);
    
    if (removed > 0) {
        identity_count -= removed;
        
        /* Save identities */
        if (identities_save(identities_path, identities, identity_count) != 0) {
            fprintf(stderr, "Error: failed to save identities file\n");
            return 1;
        }
        
        /* Audit log */
        openlog("pamhw-probe", LOG_PID, LOG_AUTHPRIV);
        syslog(LOG_NOTICE, "remove %s by %s", ident_arg, invoker);
        closelog();
    }
    
    return 0;
}

/* Main entry point */
int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    
    const char *cmd = argv[1];
    
    if (strcmp(cmd, "list") == 0) {
        return cmd_list(argc, argv);
    } else if (strcmp(cmd, "add") == 0) {
        return cmd_add(argc, argv);
    } else if (strcmp(cmd, "remove") == 0) {
        return cmd_remove(argc, argv);
    } else {
        fprintf(stderr, "Error: unknown command: %s\n", cmd);
        usage(stderr);
        return 2;
    }
}
