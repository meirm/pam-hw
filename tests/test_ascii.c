/*
 * test_ascii.c — ASCII-only character operations (locale-independence, N1)
 *
 * Identity lines must byte-match device scans made later by a different
 * process (the PAM module runs inside gdm/login/sudo, which set their own
 * locale). Folding and classification are therefore ASCII-range operations
 * that never call ctype.h. The helper contract tests below pin that; the
 * locale test activates the real scan-vs-stored mismatch trap on any host
 * that has a Turkish locale installed (glibc: tolower('I') != 'i' there).
 */
#define _DEFAULT_SOURCE
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"
#include "helpers.h"
#include "../src/ascii.h"
#include "../src/usb_detect.h"
#include "../src/identities.h"

/* Test: hw_ascii_lower folds only ASCII A-Z, passes every other byte through */
T(ascii_lower_folds_ascii_only) {
    for (int c = 'A'; c <= 'Z'; c++)
        EXPECT_INT_EQ(hw_ascii_lower(c), c + ('a' - 'A'));
    /* Non-letters are byte-for-byte unchanged — including high bytes */
    EXPECT_INT_EQ(hw_ascii_lower('a'), 'a');
    EXPECT_INT_EQ(hw_ascii_lower('0'), '0');
    EXPECT_INT_EQ(hw_ascii_lower('-'), '-');
    EXPECT_INT_EQ(hw_ascii_lower(0x80), 0x80);
    EXPECT_INT_EQ(hw_ascii_lower(0xE9), 0xE9);
    EXPECT_INT_EQ(hw_ascii_lower(0xFF), 0xFF);
}

/* Test: hw_ascii_upper folds only ASCII a-z, passes every other byte through */
T(ascii_upper_folds_ascii_only) {
    for (int c = 'a'; c <= 'z'; c++)
        EXPECT_INT_EQ(hw_ascii_upper(c), c - ('a' - 'A'));
    EXPECT_INT_EQ(hw_ascii_upper('A'), 'A');
    EXPECT_INT_EQ(hw_ascii_upper(0xE9), 0xE9);
}

/* Test: hw_ascii_isgraph accepts exactly printable non-space ASCII (0x21..0x7E) */
T(ascii_isgraph_ascii_range_only) {
    EXPECT_INT_EQ(hw_ascii_isgraph(0x20), 0);  /* space */
    EXPECT_INT_EQ(hw_ascii_isgraph(0x1F), 0);  /* control */
    EXPECT_INT_EQ(hw_ascii_isgraph(0x7F), 0);  /* DEL */
    EXPECT_INT_EQ(hw_ascii_isgraph(0x80), 0);  /* non-ASCII high byte */
    EXPECT_INT_EQ(hw_ascii_isgraph(0xE9), 0);  /* non-ASCII high byte */
    EXPECT_INT_EQ(hw_ascii_isgraph(0x21), 1);  /* '!' */
    EXPECT_INT_EQ(hw_ascii_isgraph('a'), 1);
    EXPECT_INT_EQ(hw_ascii_isgraph(0x7E), 1);  /* '~' */
}

/*
 * Test: the fold a probe stores and the fold a scan produces are identical
 * bytes — even when the scanning process runs under a Turkish locale.
 * With ctype tolower, glibc's tr_TR locales fold 'I' to something other than
 * 'i', so a stick enrolled as ...4c53i001 would scan as ...4C53I001 (UTF-8
 * quirk) or ...4c53<0xFD> (ISO-8859-9): no match, mandatory-mode lockout.
 */
T(scan_and_store_fold_identically_under_locale) {
    char dir[] = "/tmp/pamhw-ascii-XXXXXX";
    EXPECT_TRUE(mkdtemp(dir) != NULL);

    /* Probe side: enrollment runs in the C locale */
    setlocale(LC_ALL, "C");
    char stored[128];
    EXPECT_INT_EQ(identities_normalize(HW_USB, "0a12:0001:4C53I001", stored, (int)sizeof stored), 0);
    EXPECT_STR_EQ(stored, "0a12:0001:4c53i001");

    /* Scan side: the module inherits the host process locale. Activate a
     * Turkish locale when the host has one; on this host it may not. */
    const char *cands[] = { "tr_TR.UTF-8", "tr_TR.ISO-8859-9", "tr_TR", "tr_TR.iso88599" };
    const char *active = NULL;
    for (size_t i = 0; i < sizeof cands / sizeof cands[0]; i++)
        if ((active = setlocale(LC_CTYPE, cands[i])) != NULL) break;

    if (active) {
        /* Even here, ASCII folding must stay ASCII */
        EXPECT_INT_EQ(hw_ascii_lower('I'), 'i');

        hw_usb_device devs[4];
        int n = -1;
        EXPECT_INT_EQ(usb_scan(dir, devs, 4, &n), 0);
        EXPECT_INT_EQ(n, 1);
        /* The scanned ident must byte-match what the probe stored */
        EXPECT_STR_EQ(devs[0].ident, stored);
    }

    setlocale(LC_ALL, "C");
    remove_tree(dir);
}
