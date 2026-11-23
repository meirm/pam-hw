/*
 * pam_hw.c — USB hardware presence authentication module
 * 
 * Entry point that wires PAM items to core functions (config, identities,
 * USB scan, local-only guard, evaluator). All authentication logic lives in
 * the core; this file only fetches PAM items, calls the core, and logs.
 */
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <syslog.h>
#include <stddef.h>

#include "config.h"
#include "identities.h"
#include "usb_detect.h"
#include "evaluate.h"

/* Parser warning sink: malformed identities lines go to syslog */
static void identities_warn_syslog(void *ctx, int line_no, const char *reason) {
    pam_syslog((pam_handle_t *)ctx, LOG_WARNING,
               "pam_hw: identities:%d: malformed line, skipped (%s)", line_no, reason);
}

/* Parser warning sink: rejected pamhw.conf values go to syslog */
static void config_warn_syslog(void *ctx, int line_no, const char *reason) {
    pam_syslog((pam_handle_t *)ctx, LOG_WARNING, "pam_hw: pamhw.conf:%d: %s", line_no, reason);
}

/* Capacity limits for fixed-size arrays */
#define MAX_IDENTITIES 64
#define MAX_DEVICES 64

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags,
                                   int argc, const char **argv) {
    (void)flags;
    (void)argc;
    (void)argv;

    const char *user = NULL;
    const char *rhost = NULL;
    const char *tty = NULL;
    const char *service = NULL;

    /* Fetch session items (all can be NULL) */
    pam_get_item(pamh, PAM_RHOST, (const void **)&rhost);
    pam_get_item(pamh, PAM_TTY, (const void **)&tty);
    pam_get_item(pamh, PAM_SERVICE, (const void **)&service);

    /*
     * Two-phase local-only guard:
     * Phase 1 (PRE-CONFIG): Check rhost/tty remote markers with empty whitelist.
     *   Rejects remote sessions before any filesystem I/O, regardless of mode.
     * Phase 2 (POST-CONFIG): Apply cfg.services whitelist after config load.
     */
    if (!pamhw_local_only(rhost, tty, service, "")) {
        pam_syslog(pamh, LOG_WARNING,
                   "pam_hw: step aside (remote session): "
                   "rhost=%s tty=%s",
                   rhost ? rhost : "(null)",
                   tty ? tty : "(null)");
        return PAM_IGNORE;
    }

    /* Load configuration first so every internal error can follow the mode */
    hw_config cfg;
    if (config_load("/etc/pamhw/pamhw.conf", &cfg, config_warn_syslog, pamh) != 0) {
        pam_syslog(pamh, LOG_ERR, "pam_hw: config_load failed");
        return pamhw_internal_error_result(&cfg);
    }

    /* Phase 2: Apply service whitelist if configured */
    if (!pamhw_local_only(rhost, tty, service, cfg.services)) {
        if (cfg.debug) {
            pam_syslog(pamh, LOG_DEBUG,
                       "pam_hw: step aside (excluded service): "
                       "service=%s",
                       service ? service : "(null)");
        }
        return PAM_IGNORE;
    }

    if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS || user == NULL) {
        pam_syslog(pamh, LOG_WARNING, "pam_hw: failed to get PAM_USER");
        return pamhw_internal_error_result(&cfg);
    }

    if (cfg.debug) {
        pam_syslog(pamh, LOG_DEBUG,
                   "pam_hw: local session detected for user=%s, proceeding with hardware check",
                   user);
    }

    /* Load identities */
    hw_identity identities[MAX_IDENTITIES];
    int identity_count = 0;
    if (identities_load("/etc/pamhw/identities", identities, MAX_IDENTITIES, &identity_count,
                        identities_warn_syslog, pamh) != 0) {
        pam_syslog(pamh, LOG_WARNING, "pam_hw: identities_load failed");
        return pamhw_internal_error_result(&cfg);
    }

    if (identity_count == MAX_IDENTITIES) {
        pam_syslog(pamh, LOG_WARNING,
                   "pam_hw: identities capacity reached (%d), extra entries ignored",
                   MAX_IDENTITIES);
    }

    if (cfg.debug) {
        pam_syslog(pamh, LOG_DEBUG, "pam_hw: loaded %d identities", identity_count);
    }

    /* Scan USB devices */
    hw_usb_device devices[MAX_DEVICES];
    int device_count = 0;
    if (usb_scan(cfg.sys_root, devices, MAX_DEVICES, &device_count) != 0) {
        pam_syslog(pamh, LOG_WARNING, "pam_hw: usb_scan failed");
        return pamhw_internal_error_result(&cfg);
    }

    if (device_count == MAX_DEVICES) {
        pam_syslog(pamh, LOG_WARNING,
                   "pam_hw: device capacity reached (%d), extra devices ignored",
                   MAX_DEVICES);
    }

    if (cfg.debug) {
        pam_syslog(pamh, LOG_DEBUG, "pam_hw: found %d USB devices", device_count);
    }

    /* Evaluate hardware presence */
    int result = pamhw_evaluate(&cfg, identities, identity_count, user, devices, device_count);

    if (cfg.debug) {
        const char *result_str = (result == PAM_SUCCESS) ? "PAM_SUCCESS" :
                                 (result == PAM_IGNORE) ? "PAM_IGNORE" :
                                 (result == PAM_AUTH_ERR) ? "PAM_AUTH_ERR" : "UNKNOWN";
        pam_syslog(pamh, LOG_DEBUG, "pam_hw: evaluation result: %s", result_str);
    }

    return result;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags,
                              int argc, const char **argv) {
    (void)pamh;
    (void)flags;
    (void)argc;
    (void)argv;
    return PAM_SUCCESS;
}
