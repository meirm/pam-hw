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
    int ret = config_load("/nonexistent/file.conf", &cfg);
    
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
        "mode = requisite\n"
        "verify = pin\n"
        "timeout_ms = 5000\n"
        "debug = 1\n"
        "services = sshd,login\n"
        "secret_path = /etc/pamhw.sec\n"
        "mount_dir = /mnt/test\n"
        "sys_root = /sys/test\n";
    
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("requisite", cfg.mode);
    EXPECT_STR_EQ("pin", cfg.verify);
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
    int ret = config_load(path, &cfg);
    
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
    int ret = config_load(path, &cfg);
    
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
    int ret = config_load(path, &cfg);
    
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
    int ret = config_load(path, &cfg);
    
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
    int ret = config_load(path, &cfg);
    
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
    int ret = config_load(path, &cfg);
    
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
        "mode = requisite\n"
        "timeout_ms = 1500\n";
    
    write_temp_file(path, content);
    
    hw_config cfg;
    int ret = config_load(path, &cfg);
    
    EXPECT_INT_EQ(0, ret);
    EXPECT_STR_EQ("requisite", cfg.mode);
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
    int ret = config_load(path, &cfg);
    
    EXPECT_INT_EQ(0, ret);
    /* Should be truncated to 511 chars + null terminator */
    EXPECT_INT_EQ(511, (int)strlen(cfg.services));
    /* Verify it starts with 'x' characters */
    EXPECT_TRUE(cfg.services[0] == 'x');
    EXPECT_TRUE(cfg.services[510] == 'x');
    
    unlink(path);
}
