/*
 * test_probe.c — Subprocess tests for pamhw-probe
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include "test.h"
#include "helpers.h"
#include <security/pam_modules.h>
#include "../src/identities.h"
#include "../src/usb_detect.h"
#include "../src/evaluate.h"

/* Helper: run probe with args, return exit status and capture stdout */
static int run_probe(const char *const argv[], char *output, size_t output_size) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("pipe");
        exit(1);
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    
    if (pid == 0) {
        /* Child: redirect stdout to pipe, exec probe */
        unsetenv("SUDO_UID");
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        execv(argv[0], (char *const *)argv);
        perror("execv");
        _exit(127);
    }
    
    /* Parent: read stdout, wait for child */
    close(pipefd[1]);
    
    size_t total = 0;
    while (total < output_size - 1) {
        ssize_t n = read(pipefd[0], output + total, output_size - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    output[total] = '\0';
    close(pipefd[0]);
    
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/* Helper: run probe with args, capture both stdout and stderr */
static int run_probe_both(const char *const argv[], char *output, size_t output_size,
                          char *errout, size_t errout_size) {
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        perror("pipe");
        exit(1);
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    
    if (pid == 0) {
        /* Child: redirect stdout and stderr to pipes */
        unsetenv("SUDO_UID");
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        
        execv(argv[0], (char *const *)argv);
        perror("execv");
        _exit(127);
    }
    
    /* Parent: read both pipes */
    close(out_pipe[1]);
    close(err_pipe[1]);
    
    size_t total = 0;
    while (total < output_size - 1) {
        ssize_t n = read(out_pipe[0], output + total, output_size - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    output[total] = '\0';
    close(out_pipe[0]);
    
    total = 0;
    while (total < errout_size - 1) {
        ssize_t n = read(err_pipe[0], errout + total, errout_size - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    errout[total] = '\0';
    close(err_pipe[0]);
    
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/* Helper: run test-instrumented probe with env overrides */
static int run_probe_test_env(const char *const argv[], char *output, size_t output_size,
                               char *errout, size_t errout_size,
                               const char *euid_str, const char *uid_str, const char *sudo_uid_str) {
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        perror("pipe");
        exit(1);
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    
    if (pid == 0) {
        /* Child: set env overrides, redirect pipes, exec */
        if (euid_str) setenv("PAMHW_TEST_EUID", euid_str, 1);
        if (uid_str) setenv("PAMHW_TEST_UID", uid_str, 1);
        if (sudo_uid_str) setenv("SUDO_UID", sudo_uid_str, 1);
        else unsetenv("SUDO_UID");
        
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        
        execv("./pam-hw-probe-test", (char *const *)argv);
        perror("execv");
        _exit(127);
    }
    
    /* Parent: read both pipes */
    close(out_pipe[1]);
    close(err_pipe[1]);
    
    size_t total = 0;
    while (total < output_size - 1) {
        ssize_t n = read(out_pipe[0], output + total, output_size - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    output[total] = '\0';
    close(out_pipe[0]);
    
    total = 0;
    while (total < errout_size - 1) {
        ssize_t n = read(err_pipe[0], errout + total, errout_size - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    errout[total] = '\0';
    close(err_pipe[0]);
    
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/* Helper: check if string contains substring */
static int contains(const char *haystack, const char *needle) {
    return strstr(haystack, needle) != NULL;
}

/* Helper: read file contents */
static int read_file(const char *path, char *buf, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 0;
}

/* Helper: get username for UID (for test assertions) */
static const char* get_test_username(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    return pw ? pw->pw_name : NULL;
}

/* Test: probe list with default paths shows basic device info */
T(probe_list_basic) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    /* Set up fake sysfs */
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    int ret;
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    
    fake_usb(devices_dir, "1-1", "046d", "c52b", "ABC123", "Logitech Mouse");
    fake_usb(devices_dir, "2-2", "0a12", "0001", NULL, "Bluetooth Dongle");
    
    /* Create config file pointing to our sysfs */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    /* Run probe */
    const char *argv[] = {"./pam-hw-probe-test", "list", "--conf", conf_path, NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify output format: usb<whitespace><ident> */
    EXPECT_TRUE(contains(output, "usb"));
    EXPECT_TRUE(contains(output, "046d:c52b:abc123"));
    EXPECT_TRUE(contains(output, "0a12:0001"));
    
    /* Verify product name comment appears */
    EXPECT_TRUE(contains(output, "# Logitech Mouse"));
    
    remove_tree(root);
}

/* Test: enrolled device shows enrollment annotation */
T(probe_list_enrolled) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    int ret;
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    
    fake_usb(devices_dir, "1-1", "046d", "c52b", "ABC123", "Test Device");
    
    /* Create identities file */
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t046d:c52b:ABC123\talice\n");
    fprintf(f, "usb\t1234:5678\tbob\n");
    fclose(f);
    
    /* Create config */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    /* Run probe */
    const char *argv[] = {"./pam-hw-probe-test", "list", "--conf", conf_path, 
                          "--identities", identities_path, NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify enrolled annotation */
    EXPECT_TRUE(contains(output, "046d:c52b:abc123"));
    EXPECT_TRUE(contains(output, "# enrolled: alice"));
    
    remove_tree(root);
}

/* Test: --mine under sudo lists the invoker's identity lines, connected or not, bt included */
T(probe_list_mine) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    const char *username = get_test_username(1000);
    if (!username) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    
    char sysfs[512], devices_dir[600];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    mkdir(devices_dir, 0755);
    
    /* Only the first enrolled stick is connected */
    fake_usb(devices_dir, "1-1", "046d", "c52b", "MINE", "My Device");
    fake_usb(devices_dir, "2-2", "1234", "5678", "THEIRS", "Other Device");
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t046d:c52b:mine\t%s\tverify=presence\n", username);
    fprintf(f, "usb\t0781:5581:unplugged\t%s\tmode=mandatory\n", username);
    fprintf(f, "bt\tAA:BB:CC:DD:EE:FF\t%s\n", username);
    fprintf(f, "usb\t1234:5678:theirs\tother_user\n");
    fclose(f);
    
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    /* Real sudo: uid 0, SUDO_UID=1000 */
    const char *argv[] = {"./pam-hw-probe-test", "list", "--mine", "--conf", conf_path,
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);
    char expected[256];
    snprintf(expected, sizeof(expected), "usb\t046d:c52b:mine\t%s\tverify=presence\n", username);
    EXPECT_TRUE(contains(output, expected));
    snprintf(expected, sizeof(expected), "usb\t0781:5581:unplugged\t%s\tmode=mandatory\n", username);
    EXPECT_TRUE(contains(output, expected));
    snprintf(expected, sizeof(expected), "bt\tAA:BB:CC:DD:EE:FF\t%s\n", username);
    EXPECT_TRUE(contains(output, expected));
    EXPECT_TRUE(!contains(output, "theirs"));
    EXPECT_TRUE(!contains(output, "other_user"));
    
    remove_tree(root);
}

/* Test: exit 0 even when no devices found */
T(probe_list_empty) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    int ret;
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    
    /* No devices */
    
    /* Create config */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    const char *argv[] = {"./pam-hw-probe-test", "list", "--conf", conf_path, NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(0, exit_code);
    EXPECT_INT_EQ(0, strlen(output)); /* Empty output */
    
    remove_tree(root);
}

/* Test: unknown flag exits with code 2 */
T(probe_list_unknown_flag) {
    const char *argv[] = {"./pam-hw-probe", "list", "--bogus-flag", NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(2, exit_code);
}

/* Test: output line format matches requirements */
T(probe_list_output_format) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    int ret;
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    
    fake_usb(devices_dir, "1-1", "046d", "c52b", "ABC", "Test Product");
    
    /* Create config */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    const char *argv[] = {"./pam-hw-probe-test", "list", "--conf", conf_path, NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Parse first line */
    char *line = strtok(output, "\n");
    EXPECT_TRUE(line != NULL);
    
    /* Should start with "usb" */
    EXPECT_TRUE(strncmp(line, "usb", 3) == 0);
    
    /* Should have whitespace after usb */
    EXPECT_TRUE(line[3] == ' ' || line[3] == '\t');
    
    /* Should contain ident in correct format */
    EXPECT_TRUE(strstr(line, "046d:c52b:abc") != NULL);
    
    /* Should have comment with product name */
    EXPECT_TRUE(strstr(line, "# Test Product") != NULL);
    
    remove_tree(root);
}

/* === ADD/REMOVE CRUD Tests === */

/* Test: non-root add without sudo refuses */
T(probe_add_requires_sudo) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Ensure we're non-root */
    if (geteuid() == 0) {
        fprintf(stderr, "Warning: running as root, skipping non-root test\n");
        remove_tree(root);
        return;
    }
    
    const char *argv[] = {"./pam-hw-probe", "add", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_both(argv, output, sizeof(output), errout, sizeof(errout));
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "enrollment changes require sudo"));
    
    remove_tree(root);
}

/* Test: remove requires sudo when non-root */
T(probe_remove_requires_sudo) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Create a dummy identities file */
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\ttest\n");
    fclose(f);
    
    /* Ensure we're non-root */
    if (geteuid() == 0) {
        fprintf(stderr, "Warning: running as root, skipping non-root test\n");
        remove_tree(root);
        return;
    }
    
    const char *argv[] = {"./pam-hw-probe", "remove", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_both(argv, output, sizeof(output), errout, sizeof(errout));
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "enrollment changes require sudo"));
    
    remove_tree(root);
}

/* === ADD/REMOVE CRUD Tests (test-instrumented probe) === */

/* Test: add own device succeeds and appears in file */
T(probe_add_own_device) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Simulate real sudo: EUID=0, UID=0, SUDO_UID=1000 (invoker) */
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify identity appears in file */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "usb"));
    EXPECT_TRUE(contains(file_content, "1234:5678"));
    
    remove_tree(root);
}

/* Test: add with --user matching own username succeeds */
T(probe_add_user_self) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Get actual username for UID 1000 */
    const char *username = get_test_username(1000);
    if (!username) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    
    /* Simulate real sudo: EUID=0, UID=0, SUDO_UID=1000, --user <actual_username> */
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--user", username, "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify identity with correct user */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, username));
    
    remove_tree(root);
}

/* Test: add with --user OTHER by non-root refused */
T(probe_add_user_other_nonroot) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Get username for UID 1000 (invoker) */
    const char *invoker_name = get_test_username(1000);
    if (!invoker_name) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    
    /* Simulate real sudo: EUID=0, UID=0, SUDO_UID=1000 (invoker), --user root (not own) */
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--user", "root", "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "non-root users can only enroll their own devices"));
    
    /* Verify file not created */
    FILE *f = fopen(identities_path, "r");
    EXPECT_TRUE(f == NULL);
    
    remove_tree(root);
}

/* Test: root --user other succeeds */
T(probe_add_user_other_root) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Get a non-root username (use UID 1000 if it exists) */
    const char *other_user = get_test_username(1000);
    if (!other_user) {
        other_user = "nobody";  /* Fallback to nobody which usually exists */
    }
    
    /* Simulate: EUID=0, UID=0 (root), --user <other> */
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--user", other_user, "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", NULL);
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify identity enrolled for other_user */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, other_user));
    
    remove_tree(root);
}

/* Test: add normalizes identifiers (uppercase to lowercase) */
T(probe_add_normalizes) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Add with uppercase identifier */
    const char *argv[] = {"./pam-hw-probe-test", "add", "12AB:34CD",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify normalized to lowercase */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "12ab:34cd"));
    EXPECT_TRUE(!contains(file_content, "12AB:34CD"));
    
    remove_tree(root);
}

/* Test: add with invalid identifier exits 2 */
T(probe_add_invalid_ident) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Try to add malformed identifier */
    const char *argv[] = {"./pam-hw-probe-test", "add", "not-a-valid-ident",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(2, exit_code);
    EXPECT_TRUE(contains(errout, "invalid identifier format"));
    
    remove_tree(root);
}

/* Test: add AUTO with no devices fails */
T(probe_add_auto_nodev) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    /* Set up empty sysfs */
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    int ret;
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Try AUTO with no devices */
    const char *argv[] = {"./pam-hw-probe-test", "add", "AUTO",
                          "--conf", conf_path, "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "AUTO requires at least one unenrolled device"));
    
    remove_tree(root);
}

/* Test: add AUTO with ambiguous devices fails */
T(probe_add_auto_ambiguous) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    /* Set up sysfs with 2 devices */
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    int ret;
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    
    fake_usb(devices_dir, "1-1", "1234", "5678", NULL, "Device 1");
    fake_usb(devices_dir, "2-2", "abcd", "ef01", NULL, "Device 2");
    
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Try AUTO with 2 unenrolled devices */
    const char *argv[] = {"./pam-hw-probe-test", "add", "AUTO",
                          "--conf", conf_path, "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "AUTO requires exactly one unenrolled device, found 2"));
    EXPECT_TRUE(contains(errout, "1234:5678"));
    EXPECT_TRUE(contains(errout, "abcd:ef01"));
    
    remove_tree(root);
}

/* Test: add AUTO with one unenrolled device succeeds */
T(probe_add_auto_single) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    /* Set up sysfs with 1 device */
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    int ret;
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    ret = snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    if (ret < 0 || (size_t)ret >= sizeof(devices_dir)) devices_dir[sizeof(devices_dir)-1] = '\0';
    mkdir(devices_dir, 0755);
    
    fake_usb(devices_dir, "1-1", "1234", "5678", "SER123", "Test Device");
    
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* AUTO with exactly one device */
    const char *argv[] = {"./pam-hw-probe-test", "add", "AUTO",
                          "--conf", conf_path, "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify device enrolled */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "1234:5678:ser123"));
    
    remove_tree(root);
}

/* Test: add with --verify secret refused in R1 */
T(probe_add_verify_secret_refused) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Try to add with --verify secret */
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--verify", "secret", "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", NULL);
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "arrives in R3/R4"));
    
    remove_tree(root);
}

/* Test: add with --verify sign refused in R1 */
T(probe_add_verify_sign_refused) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Try to add with --verify sign */
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--verify", "sign", "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", NULL);
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "arrives in R3/R4"));
    
    remove_tree(root);
}

/* Test: add hijack attempt refused with owner name */
T(probe_add_hijack_refused) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Pre-create identities file with owner1 owning the device */
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\towner1\n");
    fclose(f);
    
    /* Try to enroll same device as different user (sudo, SUDO_UID=1000) */
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "already enrolled by user owner1"));
    
    /* Verify file unchanged (still owner1) */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "owner1"));
    
    remove_tree(root);
}

/* Test: remove own device succeeds */
T(probe_remove_own) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Get username for UID 1000 */
    const char *username = get_test_username(1000);
    if (!username) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    
    /* Pre-create identities file with user's device */
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\t%s\n", username);
    fclose(f);
    
    /* Remove as that user (sudo, SUDO_UID=1000) */
    const char *argv[] = {"./pam-hw-probe-test", "remove", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify device removed */
    char file_content[4096];
    if (read_file(identities_path, file_content, sizeof(file_content)) == 0) {
        EXPECT_TRUE(!contains(file_content, "1234:5678"));
    }
    
    remove_tree(root);
}

/* Test: remove all removes only invoker's devices, even for root */
T(probe_remove_all_scoped) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Get username for UID 1000 (invoker) */
    const char *invoker = get_test_username(1000);
    if (!invoker) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    
    /* Pre-create identities file with invoker's and other's devices */
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1111:1111\t%s\n", invoker);
    fprintf(f, "usb\t2222:2222\t%s\n", invoker);
    fprintf(f, "usb\t3333:3333\tother_user\n");
    fclose(f);
    
    /* Remove all as invoker (sudo, SUDO_UID=1000) */
    const char *argv[] = {"./pam-hw-probe-test", "remove", "all",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify only invoker's devices removed, other_user's survive */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(!contains(file_content, "1111:1111"));
    EXPECT_TRUE(!contains(file_content, "2222:2222"));
    EXPECT_TRUE(contains(file_content, "3333:3333"));
    EXPECT_TRUE(contains(file_content, "other_user"));
    
    remove_tree(root);
}

/* Test: remove other user's device as non-root unchanged */
T(probe_remove_other_nonroot) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Pre-create identities file with other_owner's device */
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\tother_owner\n");
    fclose(f);
    
    /* Try to remove other's device as non-root user (sudo, SUDO_UID=1000) */
    const char *argv[] = {"./pam-hw-probe-test", "remove", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);  /* No error, but nothing removed */
    
    /* Verify device still present */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "1234:5678"));
    EXPECT_TRUE(contains(file_content, "other_owner"));
    
    remove_tree(root);
}

/* Test: remove nonexistent device is no-op */
T(probe_remove_nonexistent) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    
    /* Get username for UID 1000 */
    const char *username = get_test_username(1000);
    if (!username) {
        username = "testuser";  /* Fallback */
    }
    
    /* Pre-create identities file with one device */
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\t%s\n", username);
    fclose(f);
    
    /* Try to remove nonexistent device */
    const char *argv[] = {"./pam-hw-probe-test", "remove", "9999:9999",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    
    EXPECT_INT_EQ(0, exit_code);  /* No error */
    
    /* Verify original device still present */
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "1234:5678"));
    
    remove_tree(root);
}

/* === Real-sudo identity model (C1): UID=0 + SUDO_UID=<invoker> === */

/* Test: under sudo (uid 0, SUDO_UID=1000) --user root is refused */
T(probe_sudo_user_other_refused) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    if (!get_test_username(1000)) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--user", "root", "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "non-root users can only enroll their own devices"));
    EXPECT_TRUE(fopen(identities_path, "r") == NULL);
    remove_tree(root);
}

/* Test: under sudo (uid 0, SUDO_UID=1000) claiming another user's device is refused */
T(probe_sudo_hijack_refused) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    if (!get_test_username(1000)) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\towner1\n");
    fclose(f);
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "already enrolled by user owner1"));
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "owner1"));
    remove_tree(root);
}

/* Test: under sudo (uid 0, SUDO_UID=1000) removing another user's line is refused */
T(probe_sudo_remove_other_refused) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    if (!get_test_username(1000)) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\tother_owner\n");
    fclose(f);
    const char *argv[] = {"./pam-hw-probe-test", "remove", "1234:5678",
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "1000");
    EXPECT_INT_EQ(0, exit_code);
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "1234:5678"));
    EXPECT_TRUE(contains(file_content, "other_owner"));
    remove_tree(root);
}

/* Test: uid 0 with no SUDO_UID is root and may enroll for another user */
T(probe_root_without_sudo_uid) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--user", "nobody", "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", NULL);
    EXPECT_INT_EQ(0, exit_code);
    char file_content[4096];
    EXPECT_INT_EQ(0, read_file(identities_path, file_content, sizeof(file_content)));
    EXPECT_TRUE(contains(file_content, "nobody"));
    remove_tree(root);
}

/* Test: malformed SUDO_UID is refused, never a silent fallback to root */
T(probe_sudo_uid_malformed_refused) {
    const char *bad[] = {"abc", "", "-1", "1000x", " 1000", "4294967296", NULL};
    for (int b = 0; bad[b]; b++) {
        char root[] = "/tmp/probe_test_XXXXXX";
        EXPECT_TRUE(mkdtemp(root) != NULL);
        char identities_path[512];
        snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
        const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                              "--user", "root", "--identities", identities_path, NULL};
        char output[4096], errout[4096];
        int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                           "0", "0", bad[b]);
        EXPECT_INT_EQ(1, exit_code);
        EXPECT_TRUE(contains(errout, "invalid SUDO_UID"));
        EXPECT_TRUE(fopen(identities_path, "r") == NULL);
        remove_tree(root);
    }
}

/* Test: SUDO_UID above 65535 is a real (non-root) invoker, not a root fallback */
T(probe_sudo_uid_large_not_root) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    if (get_test_username(4000000000u)) {
        fprintf(stderr, "Warning: UID 4000000000 exists, skipping test\n");
        remove_tree(root);
        return;
    }
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--user", "root", "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", "4000000000");
    /* No passwd entry for that uid: must fail, never enroll as root */
    EXPECT_INT_EQ(1, exit_code);
    EXPECT_TRUE(contains(errout, "could not determine invoker identity"));
    EXPECT_TRUE(fopen(identities_path, "r") == NULL);
    remove_tree(root);
}

/* === Path overrides (C2): honored only for root or the test build === */

/* Test: production probe refuses --conf/--identities/--sys-root from non-root */
T(probe_path_overrides_refused_nonroot) {
    if (getuid() == 0) {
        fprintf(stderr, "Warning: running as root, skipping non-root test\n");
        return;
    }
    const char *flags[] = {"--conf", "--identities", "--sys-root", NULL};
    for (int i = 0; flags[i]; i++) {
        const char *argv[] = {"./pam-hw-probe", "list", flags[i], "/etc/shadow", NULL};
        char output[4096], errout[4096];
        int exit_code = run_probe_both(argv, output, sizeof(output), errout, sizeof(errout));
        EXPECT_INT_EQ(1, exit_code);
        EXPECT_TRUE(contains(errout, "only honored for root"));
        EXPECT_INT_EQ(0, strlen(output));
    }
}

/* Test: probe reports malformed identities lines on stderr (I1) */
T(probe_list_warns_malformed_line) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    char identities_path[512], conf_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t1234:5678\talice\n");
    fprintf(f, "garbage\n");
    fclose(f);
    f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s/nosys\n", root);
    fclose(f);
    const char *argv[] = {"./pam-hw-probe-test", "list", "--conf", conf_path,
                          "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_both(argv, output, sizeof(output), errout, sizeof(errout));
    EXPECT_INT_EQ(0, exit_code);
    EXPECT_TRUE(contains(errout, ":2: malformed line, skipped"));
    remove_tree(root);
}

/* === Identifier / username injection through the probe (C3) === */

/* Test: newline/tab injection in add <ident> is refused and nothing is written */
T(probe_add_injection_refused) {
    const char *bad[] = {
        "x\nusb\t1234:5678:abc\troot\nusb\tc",
        "1234:5678:abc\troot",
        "1234:5678:abc root",
        "123:5678",
        NULL
    };
    if (!get_test_username(1000)) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        return;
    }
    for (int i = 0; bad[i]; i++) {
        char root[] = "/tmp/probe_test_XXXXXX";
        EXPECT_TRUE(mkdtemp(root) != NULL);
        char identities_path[512];
        snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
        const char *argv[] = {"./pam-hw-probe-test", "add", bad[i],
                              "--identities", identities_path, NULL};
        char output[4096], errout[4096];
        int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                           "0", "0", "1000");
        EXPECT_INT_EQ(2, exit_code);
        EXPECT_TRUE(contains(errout, "invalid identifier format"));
        EXPECT_TRUE(fopen(identities_path, "r") == NULL);
        remove_tree(root);
    }
}

/* Test: root --user with whitespace/control characters is refused */
T(probe_add_bad_username_refused) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    const char *argv[] = {"./pam-hw-probe-test", "add", "1234:5678",
                          "--user", "eve\nusb\t1111:2222\troot", "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    int exit_code = run_probe_test_env(argv, output, sizeof(output), errout, sizeof(errout),
                                       "0", "0", NULL);
    EXPECT_INT_EQ(2, exit_code);
    EXPECT_TRUE(contains(errout, "invalid username or identifier"));
    EXPECT_TRUE(fopen(identities_path, "r") == NULL);
    remove_tree(root);
}

/* === Serial case canonicalization (C4): README list -> add -> login flow === */

/* Test: capitalized serial: list, add <ident with caps>, save/load, evaluate => PAM_SUCCESS */
T(probe_flow_capitalized_serial_matches) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    const char *username = get_test_username(1000);
    if (!username) {
        fprintf(stderr, "Warning: UID 1000 has no username, skipping test\n");
        remove_tree(root);
        return;
    }
    
    char sysfs[512], devices_dir[600];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    mkdir(devices_dir, 0755);
    fake_usb(devices_dir, "1-1", "0781", "5581", "4C530001230925116443", "SanDisk Ultra");
    
    char conf_path[512], identities_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    /* Step 1: list shows the canonical identifier */
    const char *list_argv[] = {"./pam-hw-probe-test", "list", "--conf", conf_path,
                               "--identities", identities_path, NULL};
    char output[4096], errout[4096];
    EXPECT_INT_EQ(0, run_probe_both(list_argv, output, sizeof(output), errout, sizeof(errout)));
    EXPECT_TRUE(contains(output, "0781:5581:4c530001230925116443"));
    
    /* Step 2: user types the identifier as printed on the label (caps) */
    const char *add_argv[] = {"./pam-hw-probe-test", "add", "0781:5581:4C530001230925116443",
                              "--conf", conf_path, "--identities", identities_path, NULL};
    EXPECT_INT_EQ(0, run_probe_test_env(add_argv, output, sizeof(output), errout, sizeof(errout),
                                        "0", "0", "1000"));
    
    /* Step 3: module path: load identities, scan, evaluate */
    hw_identity ids[16];
    int n = 0;
    EXPECT_INT_EQ(0, identities_load(identities_path, ids, 16, &n, NULL, NULL));
    EXPECT_INT_EQ(1, n);
    hw_usb_device devs[16];
    int ndevs = 0;
    EXPECT_INT_EQ(0, usb_scan(sysfs, devs, 16, &ndevs));
    EXPECT_INT_EQ(1, ndevs);
    hw_config cfg;
    EXPECT_INT_EQ(0, config_load(conf_path, &cfg, NULL, NULL));
    EXPECT_INT_EQ(PAM_SUCCESS, pamhw_evaluate(&cfg, ids, n, username, devs, ndevs));
    
    /* Mandatory mode: same flow must not lock the user out */
    snprintf(cfg.mode, sizeof(cfg.mode), "mandatory");
    EXPECT_INT_EQ(PAM_SUCCESS, pamhw_evaluate(&cfg, ids, n, username, devs, ndevs));
    
    remove_tree(root);
}
