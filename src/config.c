/*
 * config.c — pamhw.conf parser implementation
 */
#define _DEFAULT_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Helper: copy string safely with truncation */
static void safe_strcpy(char *dest, const char *src, size_t size) {
    if (size == 0) return;
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

/* Helper: trim leading/trailing whitespace in place */
static void trim(char *s) {
    char *p = s;
    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    
    /* Trim trailing whitespace */
    p = s + strlen(s);
    while (p > s && isspace((unsigned char)p[-1])) p--;
    *p = '\0';
}

/* Helper: parse integer with clamping */
static int parse_int_clamped(const char *s, int min, int max, int default_val) {
    if (!s || !*s) return default_val;
    
    char *endptr;
    long val = strtol(s, &endptr, 10);
    
    /* If conversion failed, return default */
    if (endptr == s || *endptr != '\0') return default_val;
    
    /* Clamp to range */
    if (val < min) return min;
    if (val > max) return max;
    
    return (int)val;
}

/* Fill config with default values */
static void config_defaults(hw_config *cfg) {
    safe_strcpy(cfg->mode, "sufficient", sizeof(cfg->mode));
    safe_strcpy(cfg->verify, "presence", sizeof(cfg->verify));
    cfg->timeout_ms = 3000;
    cfg->debug = 0;
    safe_strcpy(cfg->services, "", sizeof(cfg->services));
    safe_strcpy(cfg->secret_path, ".pamhw.sec", sizeof(cfg->secret_path));
    safe_strcpy(cfg->mount_dir, "/run/pamhw/mnt", sizeof(cfg->mount_dir));
    safe_strcpy(cfg->sys_root, "/sys", sizeof(cfg->sys_root));
}

int config_load(const char *path, hw_config *cfg) {
    /* Fill with defaults first */
    config_defaults(cfg);
    
    /* Try to open file */
    FILE *f = fopen(path, "r");
    if (!f) {
        /* Missing file is not an error - just use defaults */
        return 0;
    }
    
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* Remove newline */
        line[strcspn(line, "\n")] = '\0';
        
        /* Trim whitespace */
        trim(line);
        
        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#') continue;
        
        /* Find '=' separator */
        char *eq = strchr(line, '=');
        if (!eq) continue;  /* No '=' means skip this line */
        
        /* Split into key and value */
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        
        /* Trim key and value */
        trim(key);
        trim(value);
        
        /* Process known keys */
        if (strcmp(key, "mode") == 0) {
            safe_strcpy(cfg->mode, value, sizeof(cfg->mode));
        } else if (strcmp(key, "verify") == 0) {
            safe_strcpy(cfg->verify, value, sizeof(cfg->verify));
        } else if (strcmp(key, "timeout_ms") == 0) {
            cfg->timeout_ms = parse_int_clamped(value, 100, 60000, cfg->timeout_ms);
        } else if (strcmp(key, "debug") == 0) {
            cfg->debug = parse_int_clamped(value, 0, 1, cfg->debug);
        } else if (strcmp(key, "services") == 0) {
            safe_strcpy(cfg->services, value, sizeof(cfg->services));
        } else if (strcmp(key, "secret_path") == 0) {
            safe_strcpy(cfg->secret_path, value, sizeof(cfg->secret_path));
        } else if (strcmp(key, "mount_dir") == 0) {
            safe_strcpy(cfg->mount_dir, value, sizeof(cfg->mount_dir));
        } else if (strcmp(key, "sys_root") == 0) {
            safe_strcpy(cfg->sys_root, value, sizeof(cfg->sys_root));
        }
        /* Unknown keys are silently ignored */
    }
    
    fclose(f);
    return 0;
}
