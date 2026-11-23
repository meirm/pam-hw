/*
 * evaluate.h — Device evaluation and PAM result aggregation
 */
#ifndef EVALUATE_H
#define EVALUATE_H

#include "config.h"
#include "identities.h"
#include "usb_detect.h"

/* Per-line evaluation result */
typedef enum {
    HW_RES_PASS,
    HW_RES_ABSENT,
    HW_RES_DENY,
    HW_RES_IGNORE
} hw_line_result;

/*
 * pamhw_local_only: Check if authentication should proceed
 * 
 * Returns:
 *   1: Proceed with hardware authentication (local session)
 *   0: Step aside (remote session or service not in whitelist)
 * 
 * Steps aside (returns 0) if:
 *   - rhost is non-empty and not localhost/127.x.x.x/::1
 *   - tty is/starts with "ssh" or equals "notty"
 *   - services_whitelist is non-empty and service is not in it
 */
int pamhw_local_only(const char *rhost, const char *tty, 
                     const char *service, const char *services_whitelist);

/*
 * pamhw_evaluate: Evaluate hardware presence and return PAM result
 * 
 * Arguments:
 *   cfg:   Configuration (mode: sufficient/mandatory)
 *   l:     Identity list
 *   n:     Number of identities
 *   user:  Username to authenticate
 *   devs:  Connected USB devices
 *   ndevs: Number of connected devices
 * 
 * Returns: PAM_SUCCESS, PAM_IGNORE, or PAM_AUTH_ERR
 * 
 * Per-device line result:
 *   - Enrolled USB device present ⇒ PASS
 *   - Enrolled USB device absent ⇒ ABSENT
 *   - HW_BT lines ⇒ IGNORE (R1 does not implement BT)
 * 
 * Aggregation (best-result-wins):
 *   1. Any line PASS ⇒ PAM_SUCCESS
 *   2. Else any line DENY, or any mandatory line ABSENT ⇒ PAM_AUTH_ERR
 *   3. Else (only sufficient lines, none present) ⇒ PAM_IGNORE
 * 
 * Special cases:
 *   - Empty cfg->mode treated as "sufficient" (fail-open default)
 *   - No identities for user: sufficient ⇒ PAM_IGNORE, mandatory ⇒ PAM_AUTH_ERR
 */
int pamhw_evaluate(const hw_config *cfg, const hw_identity *l, int n,
                   const char *user, const hw_usb_device *devs, int ndevs);

#endif /* EVALUATE_H */
