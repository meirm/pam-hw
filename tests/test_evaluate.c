/*
 * test_evaluate.c — Tests for evaluation and PAM result aggregation
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>
#include <security/pam_modules.h>
#include "test.h"
#include "../src/evaluate.h"

/* Test: Enrolled device present ⇒ PAM_SUCCESS */
T(enrolled_present_success) {
    hw_config cfg = { .mode = "sufficient" };
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "alice" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "046d:c52b:SERIAL123" }
    };
    
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_SUCCESS, result);
}

/* Test: Unenrolled stick only ⇒ PAM_IGNORE (with vs without positive/negative pair) */
T(unenrolled_stick_ignore) {
    hw_config cfg = { .mode = "sufficient" };
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "alice" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "1234:5678:DIFFERENT" }  /* Different device, not enrolled */
    };
    
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* Test: Another user's stick only ⇒ PAM_IGNORE */
T(other_user_stick_ignore) {
    hw_config cfg = { .mode = "sufficient" };
    
    hw_identity ids[2] = {
        { .type = HW_USB, .ident = "046d:c52b:ALICE_SERIAL", .user = "alice" },
        { .type = HW_USB, .ident = "1234:5678:BOB_SERIAL", .user = "bob" }
    };
    
    /* Only Bob's stick present, trying to authenticate as Alice */
    hw_usb_device devs[1] = {
        { .ident = "1234:5678:BOB_SERIAL" }
    };
    
    int result = pamhw_evaluate(&cfg, ids, 2, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* Test: Mandatory, own stick absent ⇒ PAM_AUTH_ERR */
T(mandatory_absent_deny) {
    hw_config cfg = { .mode = "mandatory" };
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "alice" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "1234:5678:DIFFERENT" }  /* Wrong device */
    };
    
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_AUTH_ERR, result);
}

/* Test: Mandatory, own stick present ⇒ PAM_SUCCESS */
T(mandatory_present_success) {
    hw_config cfg = { .mode = "mandatory" };
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "alice" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "046d:c52b:SERIAL123" }
    };
    
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_SUCCESS, result);
}

/* Test: Mandatory, unenrolled stick present ⇒ PAM_AUTH_ERR (absence semantics) */
T(mandatory_unenrolled_deny) {
    hw_config cfg = { .mode = "mandatory" };
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "alice" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "1234:5678:DIFFERENT" }
    };
    
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_AUTH_ERR, result);
}

/* Test: Aggregation - mandatory line absent + sufficient line present ⇒ PAM_SUCCESS (best-wins) */
T(aggregation_best_wins) {
    hw_config cfg = { .mode = "sufficient" };  /* Config mode, but per-line mode matters */
    
    hw_identity ids[2] = {
        { .type = HW_USB, .ident = "046d:c52b:PRESENT", .user = "alice", .mode = "sufficient" },
        { .type = HW_USB, .ident = "1234:5678:ABSENT", .user = "alice", .mode = "mandatory" }
    };
    
    /* Only the sufficient device present */
    hw_usb_device devs[1] = {
        { .ident = "046d:c52b:PRESENT" }
    };
    
    /* Best result wins: PASS from sufficient line trumps ABSENT from mandatory */
    int result = pamhw_evaluate(&cfg, ids, 2, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_SUCCESS, result);
}

/* Test: Aggregation - only sufficient lines, none present ⇒ PAM_IGNORE */
T(aggregation_all_sufficient_absent_ignore) {
    hw_config cfg = { .mode = "sufficient" };
    
    hw_identity ids[2] = {
        { .type = HW_USB, .ident = "046d:c52b:ABSENT1", .user = "alice" },
        { .type = HW_USB, .ident = "1234:5678:ABSENT2", .user = "alice" }
    };
    
    /* No devices present */
    hw_usb_device devs[1] = {
        { .ident = "9999:8888:DIFFERENT" }
    };
    
    int result = pamhw_evaluate(&cfg, ids, 2, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* Test: Internal error path - empty cfg->mode treated as sufficient ⇒ safe */
T(empty_mode_sufficient_default) {
    hw_config cfg = { .mode = "" };  /* Empty mode */
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "alice" }
    };
    
    /* No devices present */
    hw_usb_device devs[1] = {
        { .ident = "9999:8888:DIFFERENT" }
    };
    
    /* Empty mode treated as sufficient, no device ⇒ PAM_IGNORE */
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* Test: pamhw_local_only - rhost="10.0.0.5" ⇒ 0 (step aside) */
T(local_only_remote_ip) {
    int result = pamhw_local_only("10.0.0.5", "tty1", "sudo", "");
    EXPECT_INT_EQ(0, result);
}

/* Test: pamhw_local_only - rhost="localhost" ⇒ 1 (proceed) */
T(local_only_localhost) {
    int result = pamhw_local_only("localhost", "tty1", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - rhost="127.0.0.1" ⇒ 1 (proceed) */
T(local_only_127) {
    int result = pamhw_local_only("127.0.0.1", "tty1", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - rhost=NULL ⇒ 1 (proceed) */
T(local_only_null_rhost) {
    int result = pamhw_local_only(NULL, "tty1", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - rhost="" ⇒ 1 (proceed) */
T(local_only_empty_rhost) {
    int result = pamhw_local_only("", "tty1", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - tty="ssh" ⇒ 0 (step aside) */
T(local_only_tty_ssh) {
    int result = pamhw_local_only("", "ssh", "sudo", "");
    EXPECT_INT_EQ(0, result);
}

/* Test: pamhw_local_only - tty="sshx" (starts with ssh) ⇒ 0 (step aside) */
T(local_only_tty_sshx) {
    int result = pamhw_local_only("", "sshx", "sudo", "");
    EXPECT_INT_EQ(0, result);
}

/* Test: pamhw_local_only - tty="notty" ⇒ 0 (step aside) */
T(local_only_tty_notty) {
    int result = pamhw_local_only("", "notty", "sudo", "");
    EXPECT_INT_EQ(0, result);
}

/* Test: pamhw_local_only - tty="tty1" ⇒ 1 (proceed) */
T(local_only_tty_tty1) {
    int result = pamhw_local_only("", "tty1", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - tty=":0" ⇒ 1 (proceed) */
T(local_only_tty_x11) {
    int result = pamhw_local_only("", ":0", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - tty=NULL ⇒ 1 (proceed) */
T(local_only_null_tty) {
    int result = pamhw_local_only("", NULL, "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - whitelist="sudo,login", service="cron" ⇒ 0 (step aside) */
T(local_only_whitelist_not_in) {
    int result = pamhw_local_only("", "tty1", "cron", "sudo,login");
    EXPECT_INT_EQ(0, result);
}

/* Test: pamhw_local_only - whitelist="sudo,login", service="sudo" ⇒ 1 (proceed) */
T(local_only_whitelist_in) {
    int result = pamhw_local_only("", "tty1", "sudo", "sudo,login");
    EXPECT_INT_EQ(1, result);
}

/* Test: pamhw_local_only - empty whitelist ⇒ 1 (proceed) */
T(local_only_whitelist_empty) {
    int result = pamhw_local_only("", "tty1", "cron", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: No identities for user in sufficient mode ⇒ PAM_IGNORE */
T(no_identities_sufficient_ignore) {
    hw_config cfg = { .mode = "sufficient" };
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "bob" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "046d:c52b:SERIAL123" }
    };
    
    /* Trying to authenticate as alice, but only bob has identities */
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* Test: No identities for user in mandatory mode ⇒ PAM_AUTH_ERR */
T(no_identities_mandatory_deny) {
    hw_config cfg = { .mode = "mandatory" };
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "bob" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "046d:c52b:SERIAL123" }
    };
    
    /* Trying to authenticate as alice in mandatory mode with no identities */
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_AUTH_ERR, result);
}

/* Test: rhost with 127.x.x.x pattern ⇒ 1 (proceed) */
T(local_only_127_pattern) {
    int result = pamhw_local_only("127.0.1.5", "tty1", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* Test: rhost with ::1 (IPv6 loopback) ⇒ 1 (proceed) */
T(local_only_ipv6_loopback) {
    int result = pamhw_local_only("::1", "tty1", "sudo", "");
    EXPECT_INT_EQ(1, result);
}

/* C1: Test HW_BT line ignored in R1 (sufficient mode) */
T(bt_line_ignored) {
    hw_config cfg = { .mode = "sufficient" };
    
    hw_identity ids[1] = {
        { .type = HW_BT, .ident = "00:11:22:33:44:55", .user = "alice" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "046d:c52b:DIFFERENT" }
    };
    
    /* BT line never passes in R1, returns IGNORE */
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* C1: Test HW_BT line in mandatory mode */
T(bt_line_mandatory) {
    hw_config cfg = { .mode = "mandatory" };
    
    hw_identity ids[1] = {
        { .type = HW_BT, .ident = "00:11:22:33:44:55", .user = "alice", .mode = "mandatory" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "046d:c52b:DIFFERENT" }
    };
    
    /* BT line in mandatory mode: line returns IGNORE, not ABSENT */
    /* Current behavior: IGNORE doesn't trigger has_mandatory_absent, returns PAM_IGNORE */
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* I2: Unknown mode string fails open as sufficient */
T(unknown_mode_sufficient_default) {
    hw_config cfg = { .mode = "mandator" };  /* Typo: not "mandatory" */
    
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "046d:c52b:SERIAL123", .user = "alice" }
    };
    
    hw_usb_device devs[1] = {
        { .ident = "1234:5678:DIFFERENT" }  /* Device absent */
    };
    
    /* Unknown mode treated as sufficient, absent device ⇒ PAM_IGNORE */
    int result = pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1);
    EXPECT_INT_EQ(PAM_IGNORE, result);
}

/* I3: Whitelist space handling - service in whitelist with spaces */
T(local_only_whitelist_spaces) {
    /* Space after comma */
    int result1 = pamhw_local_only("", "tty1", "sudo", "sudo, login");
    EXPECT_INT_EQ(1, result1);
    
    /* Spaces around entries */
    int result2 = pamhw_local_only("", "tty1", "login", " sudo , login ");
    EXPECT_INT_EQ(1, result2);
    
    /* Service not in whitelist with spaces */
    int result3 = pamhw_local_only("", "tty1", "cron", "sudo, login");
    EXPECT_INT_EQ(0, result3);
}

/* I1: Whitelist with empty segments (edge case) */
T(local_only_whitelist_empty_segments) {
    /* Double comma creates empty segment */
    int result1 = pamhw_local_only("", "tty1", "login", "sudo,,login");
    EXPECT_INT_EQ(1, result1);
    
    /* Space-only segments */
    int result2 = pamhw_local_only("", "tty1", "sudo", "sudo, , login");
    EXPECT_INT_EQ(1, result2);
    
    /* NULL service with non-empty whitelist steps aside */
    int result3 = pamhw_local_only("", "tty1", NULL, "sudo,login");
    EXPECT_INT_EQ(0, result3);
    
    /* NULL service with empty whitelist proceeds */
    int result4 = pamhw_local_only("", "tty1", NULL, "");
    EXPECT_INT_EQ(1, result4);
}

/* === R1 is presence-only: stronger tiers fail closed (I2) === */

/* Test: hand-written verify=secret line with device present must NOT pass on presence */
T(verify_secret_present_denies) {
    hw_config cfg = { .mode = "sufficient", .verify = "presence" };
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "0a12:0001:0123456789", .user = "alice", .verify = "secret" }
    };
    hw_usb_device devs[1] = { { .ident = "0a12:0001:0123456789" } };
    EXPECT_INT_EQ(PAM_AUTH_ERR, pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1));
    snprintf(cfg.mode, sizeof(cfg.mode), "mandatory");
    EXPECT_INT_EQ(PAM_AUTH_ERR, pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1));
}

/* Test: verify=sign line absent keeps normal absent semantics for its mode */
T(verify_sign_absent_mode_semantics) {
    hw_config cfg = { .mode = "sufficient", .verify = "presence" };
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "0a12:0001", .user = "alice", .verify = "sign" }
    };
    hw_usb_device devs[1] = { { .ident = "1234:5678" } };
    EXPECT_INT_EQ(PAM_IGNORE, pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1));
    snprintf(ids[0].mode, sizeof(ids[0].mode), "mandatory");
    EXPECT_INT_EQ(PAM_AUTH_ERR, pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1));
}

/* Test: global verify=secret applies to lines without verify= and fails closed when present */
T(global_verify_secret_present_denies) {
    hw_config cfg = { .mode = "sufficient", .verify = "secret" };
    hw_identity ids[1] = {
        { .type = HW_USB, .ident = "0a12:0001", .user = "alice" }
    };
    hw_usb_device devs[1] = { { .ident = "0a12:0001" } };
    EXPECT_INT_EQ(PAM_AUTH_ERR, pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1));
    /* Explicit per-line presence overrides the global tier */
    snprintf(ids[0].verify, sizeof(ids[0].verify), "presence");
    EXPECT_INT_EQ(PAM_SUCCESS, pamhw_evaluate(&cfg, ids, 1, "alice", devs, 1));
}

/* Test: a failing secret line never poisons an honest presence line (best result wins) */
T(verify_secret_deny_does_not_poison_pass) {
    hw_config cfg = { .mode = "sufficient", .verify = "presence" };
    hw_identity ids[2] = {
        { .type = HW_USB, .ident = "0a12:0001", .user = "alice", .verify = "secret" },
        { .type = HW_USB, .ident = "1234:5678", .user = "alice" }
    };
    hw_usb_device devs[2] = { { .ident = "0a12:0001" }, { .ident = "1234:5678" } };
    EXPECT_INT_EQ(PAM_SUCCESS, pamhw_evaluate(&cfg, ids, 2, "alice", devs, 2));
}
