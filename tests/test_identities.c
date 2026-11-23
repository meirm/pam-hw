/*
 * test_identities.c — Tests for identities parser and CRUD API
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"
#include "../src/identities.h"

/* Helper: create temp file with content */
static void write_temp_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    EXPECT_TRUE(f != NULL);
    fputs(content, f);
    fclose(f);
}

/* Test: Parse valid lines (BT + USB, with/without attributes) */
T(parse_valid_lines) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = 
        "usb\t046d:c52b:123456\tuser1\tverify=secret\tmode=mandatory\n"
        "bt\tAA:BB:CC:DD:EE:FF\tuser2\n"
        "usb\t1234:5678\tuser3\tpubkey=ABCD1234\n";
    
    write_temp_file(path, content);
    
    hw_identity ids[10];
    int count = 0;
    int ret = identities_load(path, ids, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(3, count);
    
    /* First identity: USB with attributes */
    EXPECT_INT_EQ(HW_USB, ids[0].type);
    EXPECT_STR_EQ("046d:c52b:123456", ids[0].ident);
    EXPECT_STR_EQ("user1", ids[0].user);
    EXPECT_STR_EQ("secret", ids[0].verify);
    EXPECT_STR_EQ("mandatory", ids[0].mode);
    EXPECT_INT_EQ(1, ids[0].line_no);
    
    /* Second identity: BT without attributes */
    EXPECT_INT_EQ(HW_BT, ids[1].type);
    EXPECT_STR_EQ("AA:BB:CC:DD:EE:FF", ids[1].ident);
    EXPECT_STR_EQ("user2", ids[1].user);
    EXPECT_STR_EQ("", ids[1].verify);
    EXPECT_STR_EQ("", ids[1].mode);
    EXPECT_STR_EQ("", ids[1].pubkey);
    EXPECT_INT_EQ(2, ids[1].line_no);
    
    /* Third identity: USB with pubkey */
    EXPECT_INT_EQ(HW_USB, ids[2].type);
    EXPECT_STR_EQ("1234:5678", ids[2].ident);
    EXPECT_STR_EQ("user3", ids[2].user);
    EXPECT_STR_EQ("ABCD1234", ids[2].pubkey);
    EXPECT_INT_EQ(3, ids[2].line_no);
    
    unlink(path);
}

/* Test: Malformed line between valid lines (valid neighbors survive, line_no recorded) */
T(malformed_line_skipped) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = 
        "usb\t046d:c52b\tuser1\n"
        "invalid line without tabs\n"
        "usb\t1234:5678\tuser2\n";
    
    write_temp_file(path, content);
    
    hw_identity ids[10];
    int count = 0;
    int ret = identities_load(path, ids, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(2, count);
    
    /* Valid lines should load correctly */
    EXPECT_STR_EQ("046d:c52b", ids[0].ident);
    EXPECT_STR_EQ("user1", ids[0].user);
    EXPECT_INT_EQ(1, ids[0].line_no);
    
    EXPECT_STR_EQ("1234:5678", ids[1].ident);
    EXPECT_STR_EQ("user2", ids[1].user);
    EXPECT_INT_EQ(3, ids[1].line_no);
    
    unlink(path);
}

/* Warning-callback recorder for identities_load tests */
typedef struct {
    int calls;
    int line_nos[8];
    char reasons[8][128];
} warn_record;

static void record_warn(void *ctx, int line_no, const char *reason) {
    warn_record *r = ctx;
    if (r->calls < 8) {
        r->line_nos[r->calls] = line_no;
        snprintf(r->reasons[r->calls], sizeof(r->reasons[0]), "%s", reason);
    }
    r->calls++;
}

/* Test: malformed sandwich fires the warning callback with the line number (Review Focus 3) */
T(malformed_line_warns) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    write_temp_file(path,
        "usb\t046d:c52b\tuser1\n"
        "invalid line\n"
        "usb\t1234:5678\tuser2\tbogus=1\n"
        "nfc\t1234:5678\tuser2\n"
        "usb\t1234:5678\tuser2\n");
    
    hw_identity ids[10];
    int count = 0;
    warn_record rec = {0};
    EXPECT_INT_EQ(0, identities_load(path, ids, 10, &count, record_warn, &rec));
    EXPECT_INT_EQ(2, count);
    EXPECT_INT_EQ(3, rec.calls);
    EXPECT_INT_EQ(2, rec.line_nos[0]);
    EXPECT_INT_EQ(3, rec.line_nos[1]);
    EXPECT_INT_EQ(4, rec.line_nos[2]);
    EXPECT_TRUE(rec.reasons[0][0] != '\0');
    
    /* NULL callback tolerated */
    EXPECT_INT_EQ(0, identities_load(path, ids, 10, &count, NULL, NULL));
    EXPECT_INT_EQ(2, count);
    
    unlink(path);
}

/* Test: Comments and blank lines ignored */
T(comments_and_blanks_ignored) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = 
        "# This is a comment\n"
        "\n"
        "usb\t046d:c52b\tuser1\n"
        "  # Indented comment\n"
        "\n"
        "usb\t1234:5678\tuser2\n"
        "\n";
    
    write_temp_file(path, content);
    
    hw_identity ids[10];
    int count = 0;
    int ret = identities_load(path, ids, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(2, count);
    
    EXPECT_STR_EQ("user1", ids[0].user);
    EXPECT_STR_EQ("user2", ids[1].user);
    
    unlink(path);
}

/* Test: Missing file returns count=0, ret=0 */
T(missing_file_returns_zero_count) {
    hw_identity ids[10];
    int count = -1;
    int ret = identities_load("/nonexistent/file", ids, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(0, count);
}

/* Test: identities_normalize - USB lowercase with serial */
T(normalize_usb_lowercase) {
    char out[128];
    int ret = identities_normalize(HW_USB, "0A12:0001:0123456789", out, sizeof(out));
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("0a12:0001:0123456789", out);
}

/* Test: identities_normalize - USB lowercase without serial */
T(normalize_usb_no_serial) {
    char out[128];
    int ret = identities_normalize(HW_USB, "046D:C52B", out, sizeof(out));
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("046d:c52b", out);
}

/* Test: identities_normalize - BT uppercase */
T(normalize_bt_uppercase) {
    char out[128];
    int ret = identities_normalize(HW_BT, "aa:bb:cc:dd:ee:ff", out, sizeof(out));
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("AA:BB:CC:DD:EE:FF", out);
}

/* Test: identities_normalize - USB with extra fields is malformed */
T(normalize_usb_extra_fields_malformed) {
    char out[128];
    int ret = identities_normalize(HW_USB, "046d:c52b:serial:extra", out, sizeof(out));
    
    EXPECT_INT_EQ(-1, ret);
}

/* Test: identities_normalize - malformed patterns */
T(normalize_malformed) {
    char out[128];
    
    /* USB with only one part */
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "046d", out, sizeof(out)));
    
    /* BT with wrong format */
    EXPECT_INT_EQ(-1, identities_normalize(HW_BT, "AABBCCDDEEFF", out, sizeof(out)));
    
    /* Empty string */
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "", out, sizeof(out)));
}

/* Test: identities_find_user */
T(find_user) {
    hw_identity ids[3] = {
        { HW_USB, "046d:c52b", "alice", "", "", "", 1 },
        { HW_USB, "1234:5678", "bob", "", "", "", 2 },
        { HW_BT, "AA:BB:CC:DD:EE:FF", "charlie", "", "", "", 3 }
    };
    
    const hw_identity *found = identities_find_user(ids, 3, "bob");
    EXPECT_TRUE(found != NULL);
    EXPECT_STR_EQ("bob", found->user);
    EXPECT_STR_EQ("1234:5678", found->ident);
    
    /* Not found */
    found = identities_find_user(ids, 3, "dave");
    EXPECT_TRUE(found == NULL);
}

/* Test: identities_find_device */
T(find_device) {
    hw_identity ids[3] = {
        { HW_USB, "046d:c52b", "alice", "", "", "", 1 },
        { HW_USB, "1234:5678", "bob", "", "", "", 2 },
        { HW_BT, "AA:BB:CC:DD:EE:FF", "charlie", "", "", "", 3 }
    };
    
    const hw_identity *found = identities_find_device(ids, 3, HW_USB, "1234:5678");
    EXPECT_TRUE(found != NULL);
    EXPECT_STR_EQ("bob", found->user);
    
    /* Wrong type */
    found = identities_find_device(ids, 3, HW_BT, "046d:c52b");
    EXPECT_TRUE(found == NULL);
    
    /* Not found */
    found = identities_find_device(ids, 3, HW_USB, "9999:9999");
    EXPECT_TRUE(found == NULL);
}

/* Test: identities_add - own identity */
T(add_own_identity) {
    hw_identity ids[10];
    int count = 0;
    
    hw_identity new_id = { HW_USB, "046d:c52b", "alice", "pin", "always", "", 0 };
    int ret = identities_add(ids, &count, 10, &new_id, "alice", 0);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("alice", ids[0].user);
    EXPECT_STR_EQ("046d:c52b", ids[0].ident);
}

/* Test: identities_add - other user's identity (not root) */
T(add_other_user_not_root) {
    hw_identity ids[10];
    int count = 0;
    
    hw_identity new_id = { HW_USB, "046d:c52b", "bob", "", "", "", 0 };
    int ret = identities_add(ids, &count, 10, &new_id, "alice", 0);
    
    EXPECT_INT_EQ(-2, ret);
    EXPECT_INT_EQ(0, count);
}

/* Test: identities_add - hijack attempt (not root) */
T(add_hijack_not_root) {
    hw_identity ids[10];
    int count = 1;
    ids[0] = (hw_identity){ HW_USB, "046d:c52b", "bob", "", "", "", 1 };
    
    hw_identity new_id = { HW_USB, "046d:c52b", "alice", "", "", "", 0 };
    int ret = identities_add(ids, &count, 10, &new_id, "alice", 0);
    
    EXPECT_INT_EQ(-1, ret);
    EXPECT_INT_EQ(1, count);
    /* Original should be unchanged */
    EXPECT_STR_EQ("bob", ids[0].user);
}

/* Test: identities_add - hijack allowed for root */
T(add_hijack_as_root) {
    hw_identity ids[10];
    int count = 1;
    ids[0] = (hw_identity){ HW_USB, "046d:c52b", "bob", "", "", "", 1 };
    
    hw_identity new_id = { HW_USB, "046d:c52b", "alice", "", "", "", 0 };
    int ret = identities_add(ids, &count, 10, &new_id, "root", 1);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    /* Should be replaced */
    EXPECT_STR_EQ("alice", ids[0].user);
}

/* Test: identities_add - update own existing entry */
T(add_update_own) {
    hw_identity ids[10];
    int count = 1;
    ids[0] = (hw_identity){ HW_USB, "046d:c52b", "alice", "pin", "always", "", 1 };
    
    hw_identity updated = { HW_USB, "046d:c52b", "alice", "password", "conditional", "KEY123", 0 };
    int ret = identities_add(ids, &count, 10, &updated, "alice", 0);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(1, count);
    /* Should be updated */
    EXPECT_STR_EQ("password", ids[0].verify);
    EXPECT_STR_EQ("conditional", ids[0].mode);
    EXPECT_STR_EQ("KEY123", ids[0].pubkey);
}

/* Test: identities_remove - own identity */
T(remove_own_identity) {
    hw_identity ids[10];
    int count = 2;
    ids[0] = (hw_identity){ HW_USB, "046d:c52b", "alice", "", "", "", 1 };
    ids[1] = (hw_identity){ HW_USB, "1234:5678", "bob", "", "", "", 2 };
    
    int removed = identities_remove(ids, count, "046d:c52b", "alice", 0);
    
    EXPECT_INT_EQ(1, removed);
    /* Bob's identity should remain as first entry */
    EXPECT_STR_EQ("bob", ids[0].user);
    EXPECT_STR_EQ("1234:5678", ids[0].ident);
}

/* Test: identities_remove - other's identity (not root) */
T(remove_other_not_root) {
    hw_identity ids[10];
    int count = 2;
    ids[0] = (hw_identity){ HW_USB, "046d:c52b", "alice", "", "", "", 1 };
    ids[1] = (hw_identity){ HW_USB, "1234:5678", "bob", "", "", "", 2 };
    
    int removed = identities_remove(ids, count, "046d:c52b", "bob", 0);
    
    EXPECT_INT_EQ(0, removed);
    /* Nothing should be removed */
    EXPECT_STR_EQ("alice", ids[0].user);
}

/* Test: identities_remove - all own identities */
T(remove_all_own) {
    hw_identity ids[10];
    int count = 3;
    ids[0] = (hw_identity){ HW_USB, "046d:c52b", "alice", "", "", "", 1 };
    ids[1] = (hw_identity){ HW_USB, "1234:5678", "alice", "", "", "", 2 };
    ids[2] = (hw_identity){ HW_USB, "aaaa:bbbb", "bob", "", "", "", 3 };
    
    int removed = identities_remove(ids, count, "all", "alice", 0);
    
    EXPECT_INT_EQ(2, removed);
    /* Only Bob's identity should remain */
    EXPECT_STR_EQ("bob", ids[0].user);
}

/* Test: identities_save and load round-trip */
T(save_load_roundtrip) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    hw_identity original[3] = {
        { HW_USB, "046d:c52b:123456", "user1", "secret", "mandatory", "", 1 },
        { HW_BT, "AA:BB:CC:DD:EE:FF", "user2", "", "", "", 2 },
        { HW_USB, "1234:5678", "user3", "", "sufficient", "PUBKEY123", 3 }
    };
    
    int ret = identities_save(path, original, 3);
    EXPECT_INT_EQ(0, ret);
    
    hw_identity loaded[10];
    int count = 0;
    ret = identities_load(path, loaded, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(3, count);
    
    /* Verify all fields match */
    for (int i = 0; i < 3; i++) {
        EXPECT_INT_EQ(original[i].type, loaded[i].type);
        EXPECT_STR_EQ(original[i].ident, loaded[i].ident);
        EXPECT_STR_EQ(original[i].user, loaded[i].user);
        EXPECT_STR_EQ(original[i].verify, loaded[i].verify);
        EXPECT_STR_EQ(original[i].mode, loaded[i].mode);
        EXPECT_STR_EQ(original[i].pubkey, loaded[i].pubkey);
    }
    
    unlink(path);
}

/* Test: Unknown attribute in file (line should be malformed/skipped) */
T(unknown_attribute_malformed) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = 
        "usb\t046d:c52b\tuser1\n"
        "usb\t1234:5678\tuser2\tunknown=value\n"
        "usb\taaaa:bbbb\tuser3\n";
    
    write_temp_file(path, content);
    
    hw_identity ids[10];
    int count = 0;
    int ret = identities_load(path, ids, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(2, count);  /* Line 2 should be skipped */
    
    EXPECT_STR_EQ("user1", ids[0].user);
    EXPECT_STR_EQ("user3", ids[1].user);
    
    unlink(path);
}

/* Test: Parse space-separated lines (spec-compliant format) */
T(parse_space_separated) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = 
        "usb     046d:c52b:123456     user1     verify=secret     mode=mandatory\n"
        "bt     AA:BB:CC:DD:EE:FF     user2\n"
        "usb     1234:5678     user3     pubkey=ABCD1234\n";
    
    write_temp_file(path, content);
    
    hw_identity ids[10];
    int count = 0;
    int ret = identities_load(path, ids, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(3, count);
    
    /* Should parse identically to tab-separated */
    EXPECT_INT_EQ(HW_USB, ids[0].type);
    EXPECT_STR_EQ("046d:c52b:123456", ids[0].ident);
    EXPECT_STR_EQ("user1", ids[0].user);
    EXPECT_STR_EQ("secret", ids[0].verify);
    EXPECT_STR_EQ("mandatory", ids[0].mode);
    
    EXPECT_INT_EQ(HW_BT, ids[1].type);
    EXPECT_STR_EQ("AA:BB:CC:DD:EE:FF", ids[1].ident);
    EXPECT_STR_EQ("user2", ids[1].user);
    
    EXPECT_INT_EQ(HW_USB, ids[2].type);
    EXPECT_STR_EQ("1234:5678", ids[2].ident);
    EXPECT_STR_EQ("user3", ids[2].user);
    EXPECT_STR_EQ("ABCD1234", ids[2].pubkey);
    
    unlink(path);
}

/* Test: Parse mixed tab/space lines */
T(parse_mixed_whitespace) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = 
        "usb\t046d:c52b   user1\tverify=presence\n"
        "bt  \tAA:BB:CC:DD:EE:FF\tuser2\n";
    
    write_temp_file(path, content);
    
    hw_identity ids[10];
    int count = 0;
    int ret = identities_load(path, ids, 10, &count, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(2, count);
    
    EXPECT_STR_EQ("046d:c52b", ids[0].ident);
    EXPECT_STR_EQ("user1", ids[0].user);
    EXPECT_STR_EQ("presence", ids[0].verify);
    
    EXPECT_STR_EQ("AA:BB:CC:DD:EE:FF", ids[1].ident);
    EXPECT_STR_EQ("user2", ids[1].user);
    
    unlink(path);
}

/* Test: root calling remove all only removes root's own lines */
T(remove_all_root_scoped) {
    hw_identity ids[10];
    int count = 4;
    ids[0] = (hw_identity){ HW_USB, "046d:c52b", "root", "", "", "", 1 };
    ids[1] = (hw_identity){ HW_USB, "1234:5678", "alice", "", "", "", 2 };
    ids[2] = (hw_identity){ HW_USB, "aaaa:bbbb", "root", "", "", "", 3 };
    ids[3] = (hw_identity){ HW_BT, "AA:BB:CC:DD:EE:FF", "bob", "", "", "", 4 };
    
    int removed = identities_remove(ids, count, "all", "root", 1);
    
    /* Should remove only root's 2 lines */
    EXPECT_INT_EQ(2, removed);
    
    /* Alice and Bob's lines should survive */
    EXPECT_STR_EQ("alice", ids[0].user);
    EXPECT_STR_EQ("1234:5678", ids[0].ident);
    EXPECT_STR_EQ("bob", ids[1].user);
    EXPECT_STR_EQ("AA:BB:CC:DD:EE:FF", ids[1].ident);
}

/* === Strict identifier / username validation (C3) === */

/* Test: vid and pid must be exactly 4 hex digits, any case, normalized lowercase */
T(normalize_usb_hex_fields) {
    char out[128];
    EXPECT_INT_EQ(0, identities_normalize(HW_USB, "AbCd:EF01:SeRiAl", out, sizeof(out)));
    EXPECT_STR_EQ("abcd:ef01:serial", out);
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "12g4:5678", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "123:5678", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "12345:5678", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:567", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "0x12:5678", out, sizeof(out)));
}

/* Test: newline injection attempt is rejected (would forge a root line) */
T(normalize_usb_rejects_newline_injection) {
    char out[128];
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB,
        "x\nusb\t1234:5678:abc\troot\nusb\tc", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:abc\n", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:abc\r", out, sizeof(out)));
}

/* Test: tab, space and control characters anywhere in the identifier are rejected */
T(normalize_usb_rejects_whitespace_control) {
    char out[128];
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:abc\troot", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:ab cd", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:ab\x01", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:ab\x7f", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, " 1234:5678", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678:\xc3\xa9", out, sizeof(out)));
}

/* Test: oversized identifiers are rejected, never truncated into a valid one */
T(normalize_usb_rejects_oversized) {
    char raw[512], out[128];
    /* 10 chars "1234:5678:" + 117 serial = 127 total: the maximum */
    memcpy(raw, "1234:5678:", 10);
    memset(raw + 10, 'a', 117);
    raw[127] = '\0';
    EXPECT_INT_EQ(0, identities_normalize(HW_USB, raw, out, sizeof(out)));
    EXPECT_INT_EQ(127, (int)strlen(out));
    /* One more serial char is over the cap */
    memset(raw + 10, 'a', 118);
    raw[128] = '\0';
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, raw, out, sizeof(out)));
    /* Far oversized input that would truncate to a valid prefix */
    memset(raw + 10, 'a', 400);
    raw[410] = '\0';
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, raw, out, sizeof(out)));
    /* Output buffer too small to hold the result */
    char small[8];
    EXPECT_INT_EQ(-1, identities_normalize(HW_USB, "1234:5678", small, sizeof(small)));
}

/* Test: BT MAC must be six 2-hex-digit groups */
T(normalize_bt_strict) {
    char out[128];
    EXPECT_INT_EQ(0, identities_normalize(HW_BT, "dc:54:75:c2:ab:12", out, sizeof(out)));
    EXPECT_STR_EQ("DC:54:75:C2:AB:12", out);
    EXPECT_INT_EQ(-1, identities_normalize(HW_BT, "GG:BB:CC:DD:EE:FF", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_BT, "A:BB:CC:DD:EE:FFF", out, sizeof(out)));
    EXPECT_INT_EQ(-1, identities_normalize(HW_BT, "AA:BB:CC:DD:EE:F\n", out, sizeof(out)));
}

/* Test: identities_add rejects invalid usernames before anything is saved */
T(add_rejects_bad_username) {
    const char *bad[] = {"", "bad user", "bad\tuser", "bad\nuser", "bad\x01", "#root", NULL};
    for (int i = 0; bad[i]; i++) {
        hw_identity ids[4];
        int count = 0;
        hw_identity add;
        memset(&add, 0, sizeof(add));
        add.type = HW_USB;
        snprintf(add.ident, sizeof(add.ident), "1234:5678");
        snprintf(add.user, sizeof(add.user), "%s", bad[i]);
        EXPECT_INT_EQ(-3, identities_add(ids, &count, 4, &add, "root", 1));
        EXPECT_INT_EQ(0, count);
    }
}

/* Test: identities_add rejects a non-canonical or invalid identifier */
T(add_rejects_bad_ident) {
    const char *bad[] = {"1234:5678:a\nusb", "1234:5678:a\tb", "123:5678", "1234:5678:ABC", NULL};
    for (int i = 0; bad[i]; i++) {
        hw_identity ids[4];
        int count = 0;
        hw_identity add;
        memset(&add, 0, sizeof(add));
        add.type = HW_USB;
        snprintf(add.ident, sizeof(add.ident), "%s", bad[i]);
        snprintf(add.user, sizeof(add.user), "alice");
        EXPECT_INT_EQ(-3, identities_add(ids, &count, 4, &add, "alice", 0));
        EXPECT_INT_EQ(0, count);
    }
}

/* Test: loader applies the same validation (bad hex, control char in user) */
T(load_rejects_invalid_ident_and_user) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    write_temp_file(path,
        "usb\tzz12:5678\talice\n"
        "usb\t123:5678\talice\n"
        "usb\t1234:5678\tal\x01ice\n"
        "bt\tAA:BB:CC:DD:EE\talice\n"
        "usb\t1234:5678\tbob\n");
    hw_identity ids[10];
    int count = 0;
    warn_record rec = {0};
    EXPECT_INT_EQ(0, identities_load(path, ids, 10, &count, record_warn, &rec));
    EXPECT_INT_EQ(1, count);
    EXPECT_STR_EQ("bob", ids[0].user);
    EXPECT_INT_EQ(4, rec.calls);
    unlink(path);
}

/* === Canonical lowercase identifiers (C4) === */

/* Test: loader normalizes hand-written identifiers (USB lowercase, BT uppercase) */
T(load_normalizes_ident) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    write_temp_file(path,
        "usb\t0A12:0001:ABCdef\talice\n"
        "bt\taa:bb:cc:dd:ee:ff\tbob\n");
    hw_identity ids[10];
    int count = 0;
    EXPECT_INT_EQ(0, identities_load(path, ids, 10, &count, NULL, NULL));
    EXPECT_INT_EQ(2, count);
    EXPECT_STR_EQ("0a12:0001:abcdef", ids[0].ident);
    EXPECT_STR_EQ("AA:BB:CC:DD:EE:FF", ids[1].ident);
    unlink(path);
}

/* Test: remove normalizes its argument before matching */
T(remove_normalizes_argument) {
    hw_identity ids[3];
    ids[0] = (hw_identity){ HW_USB, "1234:5678:abc", "alice", "", "", "", 1 };
    ids[1] = (hw_identity){ HW_BT, "AA:BB:CC:DD:EE:FF", "alice", "", "", "", 2 };
    ids[2] = (hw_identity){ HW_USB, "aaaa:bbbb", "alice", "", "", "", 3 };
    EXPECT_INT_EQ(1, identities_remove(ids, 3, "1234:5678:ABC", "alice", 0));
    EXPECT_INT_EQ(1, identities_remove(ids, 2, "aa:bb:cc:dd:ee:ff", "alice", 0));
    EXPECT_STR_EQ("aaaa:bbbb", ids[0].ident);
}

/* === Attribute value validation (I2) === */

/* Test: unknown verify=/mode= values make the line malformed (warned, skipped) */
T(load_rejects_unknown_verify_and_mode) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    write_temp_file(path,
        "usb\t1111:1111\talice\tverify=pin\n"
        "usb\t2222:2222\talice\tmode=always\n"
        "usb\t3333:3333\talice\tverify=\n"
        "usb\t4444:4444\talice\tverify=presence\tmode=sufficient\n"
        "usb\t5555:5555\talice\tverify=secret\tmode=mandatory\n"
        "bt\tAA:BB:CC:DD:EE:FF\tbob\tverify=sign\tpubkey=/etc/pamhw/keys/bob.pub\n");
    hw_identity ids[10];
    int count = 0;
    warn_record rec = {0};
    EXPECT_INT_EQ(0, identities_load(path, ids, 10, &count, record_warn, &rec));
    EXPECT_INT_EQ(3, count);
    EXPECT_STR_EQ("4444:4444", ids[0].ident);
    EXPECT_STR_EQ("5555:5555", ids[1].ident);
    EXPECT_STR_EQ("secret", ids[1].verify);
    EXPECT_STR_EQ("sign", ids[2].verify);
    EXPECT_INT_EQ(3, rec.calls);
    EXPECT_INT_EQ(1, rec.line_nos[0]);
    EXPECT_INT_EQ(2, rec.line_nos[1]);
    EXPECT_INT_EQ(3, rec.line_nos[2]);
    unlink(path);
}

/* Test: inline comments in identities lines (README example: serial-less stick) */
T(load_inline_comment) {
    char path[] = "/tmp/identities_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    write_temp_file(path,
        "usb 1058:25a3 carol   # no serial \xe2\x80\x94 weak\n"
        "usb\t1234:5678:ab#c\tdave\tverify=presence\t# trailing\n");
    hw_identity ids[10];
    int count = 0;
    warn_record rec = {0};
    EXPECT_INT_EQ(0, identities_load(path, ids, 10, &count, record_warn, &rec));
    EXPECT_INT_EQ(0, rec.calls);
    EXPECT_INT_EQ(2, count);
    EXPECT_STR_EQ("1058:25a3", ids[0].ident);
    EXPECT_STR_EQ("carol", ids[0].user);
    EXPECT_STR_EQ("1234:5678:ab#c", ids[1].ident);
    EXPECT_STR_EQ("dave", ids[1].user);
    unlink(path);
}
