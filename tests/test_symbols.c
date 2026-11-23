/*
 * test_symbols.c — pam_hw.so exports only the PAM entry points
 */
#define _DEFAULT_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include "test.h"

/* Test: core functions are hidden; pam_sm_* stay in the dynamic symbol table */
T(module_exports_only_pam_sm) {
    void *h = dlopen("./pam_hw.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
    }
    EXPECT_TRUE(h != NULL);
    EXPECT_TRUE(dlsym(h, "pam_sm_authenticate") != NULL);
    EXPECT_TRUE(dlsym(h, "pam_sm_setcred") != NULL);
    const char *hidden[] = {
        "config_load", "identities_load", "identities_normalize", "identities_add",
        "identities_remove", "identities_save", "identities_find_user",
        "identities_find_device", "usb_scan", "pamhw_evaluate", "pamhw_local_only",
        "pamhw_internal_error_result", NULL
    };
    for (int i = 0; hidden[i]; i++) {
        if (dlsym(h, hidden[i]) != NULL) {
            fprintf(stderr, "exported: %s\n", hidden[i]);
        }
        EXPECT_TRUE(dlsym(h, hidden[i]) == NULL);
    }
    dlclose(h);
}
