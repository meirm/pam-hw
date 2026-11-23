/*
 * config.h — pamhw.conf parser interface
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "warn.h"

typedef struct {
    char mode[16];
    char verify[16];
    int timeout_ms;
    int debug;
    char services[512];
    char secret_path[128];
    char mount_dir[256];
    char sys_root[256];
} hw_config;

/*
 * Load configuration from file.
 * 
 * Fills cfg with defaults first (mode=sufficient, verify=presence,
 * timeout_ms=3000, debug=0, services="", secret_path=".pamhw.sec",
 * mount_dir="/run/pamhw/mnt", sys_root="/sys"), then applies key=value
 * lines from file.
 * 
 * - Missing file returns 0 with defaults (not an error)
 * - Unknown keys are silently ignored
 * - Comments (#) and blank lines allowed; a '#' at line start or preceded
 *   by whitespace starts a comment that runs to end of line
 * - Spaces around '=' tolerated
 * - mode: sufficient|mandatory; verify: presence|secret|sign;
 *   debug: true|false|yes|no|1|0. Any other value is reported through
 *   warn (NULL tolerated) and the default is kept.
 * - timeout_ms clamped to [100, 60000]
 * - Values that overflow field size are truncated safely
 * 
 * Returns 0 on success.
 */
int config_load(const char *path, hw_config *cfg, pamhw_warn_fn warn, void *warn_ctx);

#endif /* CONFIG_H */
