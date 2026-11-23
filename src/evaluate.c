/*
 * evaluate.c — Device evaluation and PAM result aggregation
 */
#include "evaluate.h"
#include <security/pam_modules.h>
#include <string.h>

/* Helper: Check if string starts with prefix */
static int starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

/* Helper: Check if service is in whitelist (comma-separated) */
static int service_in_whitelist(const char *service, const char *whitelist) {
    if (!service || !whitelist || *whitelist == '\0') return 0;
    
    char wl_copy[512];
    strncpy(wl_copy, whitelist, sizeof(wl_copy) - 1);
    wl_copy[sizeof(wl_copy) - 1] = '\0';
    
    char *token = strtok(wl_copy, ",");
    while (token) {
        /* Strip leading/trailing spaces */
        while (*token == ' ') token++;
        
        /* Skip empty segments (e.g., from "sudo,,login" or " ") */
        if (*token == '\0') {
            token = strtok(NULL, ",");
            continue;
        }
        
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';
        
        if (strcmp(token, service) == 0) {
            return 1;
        }
        token = strtok(NULL, ",");
    }
    return 0;
}

int pamhw_local_only(const char *rhost, const char *tty,
                     const char *service, const char *services_whitelist) {
    /* Check rhost: non-empty and not localhost/127.x/::1 => remote */
    if (rhost && *rhost != '\0') {
        if (strcmp(rhost, "localhost") != 0 &&
            !starts_with(rhost, "127.") &&
            strcmp(rhost, "::1") != 0) {
            return 0;  /* Remote host */
        }
    }
    
    /* Check tty: ssh* or notty => remote/pseudo */
    if (tty && *tty != '\0') {
        if (starts_with(tty, "ssh") || strcmp(tty, "notty") == 0) {
            return 0;
        }
    }
    
    /* Check services whitelist */
    if (services_whitelist && *services_whitelist != '\0') {
        if (!service || !service_in_whitelist(service, services_whitelist)) {
            return 0;
        }
    }
    
    return 1;  /* Proceed with hardware authentication */
}

/* Helper: Evaluate single line */
static hw_line_result evaluate_line(const hw_identity *line,
                                   const hw_usb_device *devs, int ndevs) {
    /* HW_BT lines return IGNORE in R1 */
    if (line->type == HW_BT) {
        return HW_RES_IGNORE;
    }
    
    /* HW_USB: check if device present */
    for (int i = 0; i < ndevs; i++) {
        if (strcmp(line->ident, devs[i].ident) == 0) {
            return HW_RES_PASS;  /* Device present */
        }
    }
    
    return HW_RES_ABSENT;  /* Device not found */
}

int pamhw_evaluate(const hw_config *cfg, const hw_identity *l, int n,
                   const char *user, const hw_usb_device *devs, int ndevs) {
    /* Determine effective mode (empty => sufficient) */
    const char *effective_mode = (cfg->mode[0] == '\0') ? "sufficient" : cfg->mode;
    int is_mandatory = (strcmp(effective_mode, "mandatory") == 0);
    
    /* Count user's lines and evaluate them */
    int user_lines = 0;
    int has_pass = 0;
    int has_deny = 0;
    int has_mandatory_absent = 0;
    
    for (int i = 0; i < n; i++) {
        if (strcmp(l[i].user, user) != 0) continue;
        
        user_lines++;
        hw_line_result res = evaluate_line(&l[i], devs, ndevs);
        
        /* Determine line mode (use per-line mode if set, else global) */
        const char *line_mode = (l[i].mode[0] != '\0') ? l[i].mode : effective_mode;
        int line_mandatory = (strcmp(line_mode, "mandatory") == 0);
        
        switch (res) {
            case HW_RES_PASS:
                has_pass = 1;
                break;
            case HW_RES_DENY:
                has_deny = 1;
                break;
            case HW_RES_ABSENT:
                if (line_mandatory) {
                    has_mandatory_absent = 1;
                }
                break;
            case HW_RES_IGNORE:
                /* Ignore BT lines */
                break;
        }
    }
    
    /* No identities for user */
    if (user_lines == 0) {
        return is_mandatory ? PAM_AUTH_ERR : PAM_IGNORE;
    }
    
    /* Aggregation: best result wins */
    if (has_pass) {
        return PAM_SUCCESS;
    }
    if (has_deny || has_mandatory_absent) {
        return PAM_AUTH_ERR;
    }
    return PAM_IGNORE;
}
