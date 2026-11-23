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
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        execv("./pam-hw-probe", (char *const *)argv);
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
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        
        execv("./pam-hw-probe", (char *const *)argv);
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

/* Test: probe list with default paths shows basic device info */
T(probe_list_basic) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    /* Set up fake sysfs */
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
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
    const char *argv[] = {"./pam-hw-probe", "list", "--conf", conf_path, NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify output format: usb<whitespace><ident> */
    EXPECT_TRUE(contains(output, "usb"));
    EXPECT_TRUE(contains(output, "046d:c52b:ABC123"));
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
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
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
    const char *argv[] = {"./pam-hw-probe", "list", "--conf", conf_path, 
                          "--identities", identities_path, NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Verify enrolled annotation */
    EXPECT_TRUE(contains(output, "046d:c52b:ABC123"));
    EXPECT_TRUE(contains(output, "# enrolled: alice"));
    
    remove_tree(root);
}

/* Test: --mine flag filters to current user's devices */
T(probe_list_mine) {
    char root[] = "/tmp/probe_test_XXXXXX";
    EXPECT_TRUE(mkdtemp(root) != NULL);
    
    char sysfs[512];
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    mkdir(sysfs, 0755);
    
    char devices_dir[512];
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    mkdir(devices_dir, 0755);
    
    fake_usb(devices_dir, "1-1", "046d", "c52b", "MINE", "My Device");
    fake_usb(devices_dir, "2-2", "1234", "5678", "THEIRS", "Other Device");
    
    /* Get current username */
    char username[64];
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) {
        fprintf(stderr, "Warning: getpwuid failed for --mine test\n");
        remove_tree(root);
        return;
    }
    snprintf(username, sizeof(username), "%s", pw->pw_name);
    
    /* Create identities file */
    char identities_path[512];
    snprintf(identities_path, sizeof(identities_path), "%s/identities", root);
    FILE *f = fopen(identities_path, "w");
    fprintf(f, "usb\t046d:c52b:MINE\t%s\n", username);
    fprintf(f, "usb\t1234:5678:THEIRS\tother_user\n");
    fclose(f);
    
    /* Create config */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    /* Run probe with --mine */
    const char *argv[] = {"./pam-hw-probe", "list", "--mine", "--conf", conf_path,
                          "--identities", identities_path, NULL};
    char output[4096];
    int exit_code = run_probe(argv, output, sizeof(output));
    
    EXPECT_INT_EQ(0, exit_code);
    
    /* Should only show current user's device */
    EXPECT_TRUE(contains(output, "046d:c52b:MINE"));
    EXPECT_TRUE(!contains(output, "1234:5678:THEIRS"));
    
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
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    mkdir(devices_dir, 0755);
    
    /* No devices */
    
    /* Create config */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    const char *argv[] = {"./pam-hw-probe", "list", "--conf", conf_path, NULL};
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
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb", sysfs);
    mkdir(devices_dir, 0755);
    snprintf(devices_dir, sizeof(devices_dir), "%s/bus/usb/devices", sysfs);
    mkdir(devices_dir, 0755);
    
    fake_usb(devices_dir, "1-1", "046d", "c52b", "ABC", "Test Product");
    
    /* Create config */
    char conf_path[512];
    snprintf(conf_path, sizeof(conf_path), "%s/pamhw.conf", root);
    FILE *f = fopen(conf_path, "w");
    fprintf(f, "sys_root=%s\n", sysfs);
    fclose(f);
    
    const char *argv[] = {"./pam-hw-probe", "list", "--conf", conf_path, NULL};
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
    EXPECT_TRUE(strstr(line, "046d:c52b:ABC") != NULL);
    
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

/* Placeholder tests for documented behaviors (require sudo/fakeroot harness) */

/* Test: add own device succeeds */
T(probe_add_own_device_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add with --user matching own username */
T(probe_add_user_self_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add with --user OTHER by non-root refused */
T(probe_add_user_other_nonroot_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add normalizes identifiers */
T(probe_add_normalizes_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add with invalid identifier exits 2 */
T(probe_add_invalid_ident_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add AUTO with no devices fails */
T(probe_add_auto_nodev_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add AUTO with ambiguous devices fails */
T(probe_add_auto_ambiguous_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add AUTO with one unenrolled device succeeds */
T(probe_add_auto_single_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add with --verify secret refused in R1 */
T(probe_add_verify_secret_refused_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add with --verify sign refused in R1 */
T(probe_add_verify_sign_refused_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: add hijack attempt refused with owner name */
T(probe_add_hijack_refused_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: remove own device succeeds */
T(probe_remove_own_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: remove all removes only own devices */
T(probe_remove_all_scoped_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: remove other user's device as non-root unchanged */
T(probe_remove_other_nonroot_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}

/* Test: remove nonexistent device is no-op */
T(probe_remove_nonexistent_placeholder) {
    /* Requires sudo test harness - placeholder for now */
}
