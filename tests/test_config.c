/*
 * test_config.c — Tests for pamhw.conf parser
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"
#include "../src/config.h"

/* Helper: create temp file with content */
static void write_temp_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    EXPECT_TRUE(f != NULL);
    fputs(content, f);
    fclose(f);
}

/* Test: Missing file returns defaults */
T(missing_file_defaults) {
    hw_config cfg;
    int ret = config_load("/nonexistent/file.conf", &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("sufficient", cfg.mode);
    EXPECT_STR_EQ("presence", cfg.verify);
    EXPECT_INT_EQ(3000, cfg.timeout_ms);
    EXPECT_INT_EQ(0, cfg.debug);
    EXPECT_STR_EQ("", cfg.services);
    EXPECT_STR_EQ(".pamhw.sec", cfg.secret_path);
    EXPECT_STR_EQ("/run/pamhw/mnt", cfg.mount_dir);
    EXPECT_STR_EQ("/sys", cfg.sys_root);
}

/* Test: Full file sets all fields */
T(full_file_all_fields) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content =
        "mode = mandatory\n"
        "verify = secret\n"
        "timeout_ms = 5000\n"
        "debug = 1\n"
        "services = sshd,login\n"
        "secret_path = /etc/pamhw.sec\n"
        "mount_dir = /mnt/test\n"
        "sys_root = /sys/test\n";
    
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("mandatory", cfg.mode);
    EXPECT_STR_EQ("secret", cfg.verify);
    EXPECT_INT_EQ(5000, cfg.timeout_ms);
    EXPECT_INT_EQ(1, cfg.debug);
    EXPECT_STR_EQ("sshd,login", cfg.services);
    EXPECT_STR_EQ("/etc/pamhw.sec", cfg.secret_path);
    EXPECT_STR_EQ("/mnt/test", cfg.mount_dir);
    EXPECT_STR_EQ("/sys/test", cfg.sys_root);
    
    unlink(path);
}

/* Test: timeout_ms clamps to minimum (100) */
T(timeout_clamp_min) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = "timeout_ms = 0\n";
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(100, cfg.timeout_ms);
    
    unlink(path);
}

/* Test: timeout_ms clamps to maximum (60000) */
T(timeout_clamp_max) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content = "timeout_ms = 999999\n";
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_INT_EQ(60000, cfg.timeout_ms);
    
    unlink(path);
}

/* Test: Unknown keys ignored */
T(unknown_key_ignored) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content =
        "mode = sufficient\n"
        "unknown_key = some_value\n"
        "timeout_ms = 2000\n";
    
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("sufficient", cfg.mode);
    EXPECT_INT_EQ(2000, cfg.timeout_ms);
    
    unlink(path);
}

/* Test: Comments and blank lines ignored */
T(comments_and_blanks) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content =
        "# This is a comment\n"
        "\n"
        "mode = sufficient\n"
        "  # Indented comment\n"
        "\n"
        "timeout_ms = 4000\n"
        "\n";
    
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("sufficient", cfg.mode);
    EXPECT_INT_EQ(4000, cfg.timeout_ms);
    
    unlink(path);
}

/* Test: Spaces around equals sign */
T(spaces_around_equals) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content =
        "mode=sufficient\n"
        "verify = presence\n"
        "timeout_ms =2500\n"
        "debug= 1\n"
        "services  =  sshd\n";
    
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("sufficient", cfg.mode);
    EXPECT_STR_EQ("presence", cfg.verify);
    EXPECT_INT_EQ(2500, cfg.timeout_ms);
    EXPECT_INT_EQ(1, cfg.debug);
    EXPECT_STR_EQ("sshd", cfg.services);
    
    unlink(path);
}

/* Test: Empty config file returns defaults */
T(empty_file_defaults) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    write_temp_file(path, "");
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("sufficient", cfg.mode);
    EXPECT_INT_EQ(3000, cfg.timeout_ms);
    
    unlink(path);
}

/* Test: Partial config fills remaining with defaults */
T(partial_config_defaults) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    const char *content =
        "mode = mandatory\n"
        "timeout_ms = 1500\n";
    
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("mandatory", cfg.mode);
    EXPECT_INT_EQ(1500, cfg.timeout_ms);
    /* Verify defaults for unspecified fields */
    EXPECT_STR_EQ("presence", cfg.verify);
    EXPECT_INT_EQ(0, cfg.debug);
    EXPECT_STR_EQ("", cfg.services);
    
    unlink(path);
}

/* Test: Value overflow is truncated safely */
T(value_overflow_truncated) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    
    /* Create a very long string (more than 512 chars) */
    char long_services[1024];
    memset(long_services, 'x', sizeof(long_services) - 1);
    long_services[sizeof(long_services) - 1] = '\0';
    
    char content[2048];
    snprintf(content, sizeof(content), "services = %s\n", long_services);
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg, NULL, NULL);
    
    EXPECT_INT_EQ(0, ret);
    /* Should be truncated to 511 chars + null terminator */
    EXPECT_INT_EQ(511, (int)strlen(cfg.services));
    /* Verify it starts with 'x' characters */
    EXPECT_TRUE(cfg.services[0] == 'x');
    EXPECT_TRUE(cfg.services[510] == 'x');
    
    unlink(path);
}

/* Warning-callback recorder for config_load tests */
typedef struct {
    int calls;
    int line_nos[8];
} cfg_warn_record;

static void record_cfg_warn(void *ctx, int line_no, const char *reason) {
    cfg_warn_record *r = ctx;
    (void)reason;
    if (r->calls < 8) r->line_nos[r->calls] = line_no;
    r->calls++;
}

/* Test: the spec's example pamhw.conf, verbatim, parses to the documented values (I3) */
T(spec_example_config_verbatim) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    write_temp_file(path,
        "mode = sufficient                    # sufficient | mandatory\n"
        "verify = presence                    # default tier\n"
        "timeout_ms = 3000\n"
        "debug = false\n"
        "services =                           # optional whitelist; empty = all local services\n"
        "secret_path = .pamhw.sec   # file on stick, relative to mount root\n"
        "mount_dir = /run/pamhw/mnt    # private read-only mount point\n"
        "sys_root = /sys                      # overridable for tests\n");
    hw_config cfg;
    cfg_warn_record rec = {0};
    EXPECT_INT_EQ(0, config_load(path, &cfg, record_cfg_warn, &rec));
    EXPECT_STR_EQ("sufficient", cfg.mode);
    EXPECT_STR_EQ("presence", cfg.verify);
    EXPECT_INT_EQ(3000, cfg.timeout_ms);
    EXPECT_INT_EQ(0, cfg.debug);
    EXPECT_STR_EQ("", cfg.services);
    EXPECT_STR_EQ(".pamhw.sec", cfg.secret_path);
    EXPECT_STR_EQ("/run/pamhw/mnt", cfg.mount_dir);
    EXPECT_STR_EQ("/sys", cfg.sys_root);
    EXPECT_INT_EQ(0, rec.calls);
    unlink(path);
}

/* Test: inline comment after mode = mandatory keeps mandatory (no silent fail-open) */
T(inline_comment_mandatory_and_debug_true) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    write_temp_file(path,
        "mode = mandatory  # sufficient | mandatory\n"
        "debug = true\t# verbose\n"
        "secret_path = a#b\n");
    hw_config cfg;
    EXPECT_INT_EQ(0, config_load(path, &cfg, NULL, NULL));
    EXPECT_STR_EQ("mandatory", cfg.mode);
    EXPECT_INT_EQ(1, cfg.debug);
    /* '#' not preceded by whitespace is part of the value */
    EXPECT_STR_EQ("a#b", cfg.secret_path);
    unlink(path);
}

/* Test: debug accepts true/false/yes/no/1/0; anything else warns and keeps the default */
T(debug_boolean_values) {
    const char *on[] = {"true", "yes", "1", NULL};
    const char *off[] = {"false", "no", "0", NULL};
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    char content[64];
    hw_config cfg;
    for (int i = 0; on[i]; i++) {
        snprintf(content, sizeof(content), "debug = %s\n", on[i]);
        write_temp_file(path, content);
        EXPECT_INT_EQ(0, config_load(path, &cfg, NULL, NULL));
        EXPECT_INT_EQ(1, cfg.debug);
    }
    for (int i = 0; off[i]; i++) {
        snprintf(content, sizeof(content), "debug = true\ndebug = %s\n", off[i]);
        write_temp_file(path, content);
        EXPECT_INT_EQ(0, config_load(path, &cfg, NULL, NULL));
        EXPECT_INT_EQ(0, cfg.debug);
    }
    cfg_warn_record rec = {0};
    write_temp_file(path, "debug = maybe\n");
    EXPECT_INT_EQ(0, config_load(path, &cfg, record_cfg_warn, &rec));
    EXPECT_INT_EQ(0, cfg.debug);
    EXPECT_INT_EQ(1, rec.calls);
    EXPECT_INT_EQ(1, rec.line_nos[0]);
    unlink(path);
}

/* Test: unknown mode / verify values warn and keep defaults; known tiers are stored */
T(mode_verify_strict_values) {
    char path[] = "/tmp/config_test_XXXXXX";
    int fd = mkstemp(path);
    EXPECT_TRUE(fd >= 0);
    close(fd);
    hw_config cfg;
    cfg_warn_record rec = {0};
    write_temp_file(path,
        "# comment\n"
        "mode = mandator\n"
        "verify = secrte\n");
    EXPECT_INT_EQ(0, config_load(path, &cfg, record_cfg_warn, &rec));
    EXPECT_STR_EQ("sufficient", cfg.mode);
    EXPECT_STR_EQ("presence", cfg.verify);
    EXPECT_INT_EQ(2, rec.calls);
    EXPECT_INT_EQ(2, rec.line_nos[0]);
    EXPECT_INT_EQ(3, rec.line_nos[1]);
    
    /* Known stronger tiers are kept so the evaluator fails closed on them */
    write_temp_file(path, "verify = sign\n");
    EXPECT_INT_EQ(0, config_load(path, &cfg, NULL, NULL));
    EXPECT_STR_EQ("sign", cfg.verify);
    unlink(path);
}
