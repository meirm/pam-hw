/*
 * identities.c — Hardware identity parser and CRUD implementation
 */
#define _DEFAULT_SOURCE
#include "identities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

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

/* Helper: case-insensitive string comparison prefix */
static int is_attr(const char *field, const char *name) {
    size_t len = strlen(name);
    return strncmp(field, name, len) == 0 && field[len] == '=';
}

/* Helper: copy string safely with truncation */
static void safe_strcpy(char *dest, const char *src, size_t size) {
    int ret;
    if (size == 0) return;
    ret = snprintf(dest, size, "%s", src);
    if (ret < 0 || (size_t)ret >= size) {
        dest[size - 1] = '\0';  /* Explicit truncation */
    }
}

/* Helper: exactly len hex digits */
static int is_hex_run(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) return 0;
    }
    return 1;
}

/*
 * Helper: username as stored in an identities line: non-empty, fits
 * hw_identity.user, printable non-space ASCII only, and not starting
 * with '#' (which the loader would read as a comment).
 */
static int is_valid_username(const char *user) {
    size_t len = strlen(user);
    if (len == 0 || len >= sizeof(((hw_identity *)0)->user)) return 0;
    if (user[0] == '#') return 0;
    for (size_t i = 0; i < len; i++) {
        if (!isgraph((unsigned char)user[i])) return 0;
    }
    return 1;
}

int identities_normalize(hw_type t, const char *raw, char *out, int max) {
    if (!raw || !out || max < 1) return -1;

    size_t len = strlen(raw);
    if (len == 0 || len > HW_IDENT_MAX || len + 1 > (size_t)max) return -1;

    /* No whitespace, control or non-ASCII bytes anywhere */
    for (size_t i = 0; i < len; i++) {
        if (!isgraph((unsigned char)raw[i])) return -1;
    }

    if (t == HW_USB) {
        /* USB format: vid:pid[:serial], vid/pid exactly 4 hex digits */
        int colons = 0;
        for (const char *p = raw; *p; p++) {
            if (*p == ':') colons++;
        }
        if (colons < 1 || colons > 2) return -1;

        if (len < 9 || raw[4] != ':' || !is_hex_run(raw, 4) || !is_hex_run(raw + 5, 4)) {
            return -1;
        }
        if (len > 9) {
            /* Serial present: ":" then at least one character */
            if (raw[9] != ':' || len == 10) return -1;
        }
    } else if (t == HW_BT) {
        /* BT format: AA:BB:CC:DD:EE:FF */
        if (len != 17) return -1;
        for (size_t i = 0; i < len; i++) {
            if (i % 3 == 2) {
                if (raw[i] != ':') return -1;
            } else if (!isxdigit((unsigned char)raw[i])) {
                return -1;
            }
        }
    } else {
        return -1;
    }

    /* USB lowercase, BT uppercase */
    for (size_t i = 0; i <= len; i++) {
        out[i] = (t == HW_USB) ? (char)tolower((unsigned char)raw[i])
                               : (char)toupper((unsigned char)raw[i]);
    }
    return 0;
}

/* Helper: report a skipped malformed line (callback optional) */
static void warn_line(pamhw_warn_fn warn, void *warn_ctx, int line_no, const char *reason) {
    if (warn) warn(warn_ctx, line_no, reason);
}

int identities_load(const char *path, hw_identity *out, int max, int *count,
                    pamhw_warn_fn warn, void *warn_ctx) {
    *count = 0;
    
    FILE *f = fopen(path, "r");
    if (!f) {
        /* Missing file is not an error */
        return 0;
    }
    
    char line[1024];
    int line_no = 0;
    
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        
        /* Remove newline */
        line[strcspn(line, "\n")] = '\0';
        
        /* Trim whitespace */
        trim(line);
        
        /* Skip empty lines and comments */
        if (line[0] == '\0' || line[0] == '#') continue;
        
        /* Parse line: type<TAB>ident<TAB>user[<TAB>attr=value]... */
        char type_str[32], ident[128], user[64];
        type_str[0] = ident[0] = user[0] = '\0';
        
        /* Tokenize by whitespace (spaces or tabs) */
        char *tokens[32];
        int ntokens = 0;
        char *p = line;
        while (*p && ntokens < 32) {
            /* Skip leading whitespace */
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            
            /* Mark token start */
            tokens[ntokens++] = p;
            
            /* Find end of token (next whitespace) */
            while (*p && !isspace((unsigned char)*p)) p++;
            
            /* Null-terminate token if not at end */
            if (*p) {
                *p = '\0';
                p++;
            }
        }
        
        /* Must have at least type, ident, user */
        if (ntokens < 3) {
            warn_line(warn, warn_ctx, line_no, "expected: type identifier user [attr=value ...]");
            continue;
        }
        
        safe_strcpy(type_str, tokens[0], sizeof(type_str));
        safe_strcpy(ident, tokens[1], sizeof(ident));
        safe_strcpy(user, tokens[2], sizeof(user));
        
        /* Parse type */
        hw_type type;
        if (strcmp(type_str, "usb") == 0) {
            type = HW_USB;
        } else if (strcmp(type_str, "bt") == 0) {
            type = HW_BT;
        } else {
            warn_line(warn, warn_ctx, line_no, "unknown device type");
            continue;
        }
        
        /* Identifier and username: same validation as identities_add */
        char canon[HW_IDENT_MAX + 1];
        if (identities_normalize(type, tokens[1], canon, sizeof(canon)) != 0) {
            warn_line(warn, warn_ctx, line_no, "invalid identifier");
            continue;
        }
        if (!is_valid_username(tokens[2])) {
            warn_line(warn, warn_ctx, line_no, "invalid username");
            continue;
        }
        
        /* Parse attributes */
        hw_identity id;
        memset(&id, 0, sizeof(id));
        id.type = type;
        safe_strcpy(id.ident, ident, sizeof(id.ident));
        safe_strcpy(id.user, user, sizeof(id.user));
        id.line_no = line_no;
        
        int malformed_attr = 0;
        for (int i = 3; i < ntokens; i++) {
            char *field = tokens[i];
            if (is_attr(field, "verify")) {
                safe_strcpy(id.verify, field + 7, sizeof(id.verify));
            } else if (is_attr(field, "mode")) {
                safe_strcpy(id.mode, field + 5, sizeof(id.mode));
            } else if (is_attr(field, "pubkey")) {
                safe_strcpy(id.pubkey, field + 7, sizeof(id.pubkey));
            } else {
                /* Unknown attribute - line is malformed */
                malformed_attr = 1;
                break;
            }
        }
        
        if (malformed_attr) {
            warn_line(warn, warn_ctx, line_no, "unknown attribute");
            continue;
        }
        
        /* Add to output if space available */
        if (*count < max) {
            out[*count] = id;
            (*count)++;
        }
    }
    
    fclose(f);
    return 0;
}

const hw_identity *identities_find_user(const hw_identity *l, int n, const char *user) {
    for (int i = 0; i < n; i++) {
        if (strcmp(l[i].user, user) == 0) {
            return &l[i];
        }
    }
    return NULL;
}

const hw_identity *identities_find_device(const hw_identity *l, int n, hw_type t, const char *ident) {
    for (int i = 0; i < n; i++) {
        if (l[i].type == t && strcmp(l[i].ident, ident) == 0) {
            return &l[i];
        }
    }
    return NULL;
}

int identities_add(hw_identity *l, int *n, int max, const hw_identity *add,
                   const char *invoker, int is_root) {
    /* Reject anything that would not survive a save/load round trip */
    char canon[HW_IDENT_MAX + 1];
    if (!is_valid_username(add->user) ||
        identities_normalize(add->type, add->ident, canon, sizeof(canon)) != 0 ||
        strcmp(canon, add->ident) != 0) {
        return -3;
    }
    
    /* Check if caller can add this user's identity */
    if (!is_root && strcmp(add->user, invoker) != 0) {
        return -2;  /* Not own username */
    }
    
    /* Check for existing entry with same device */
    for (int i = 0; i < *n; i++) {
        if (l[i].type == add->type && strcmp(l[i].ident, add->ident) == 0) {
            /* Found existing entry */
            if (!is_root && strcmp(l[i].user, invoker) != 0) {
                return -1;  /* Hijack attempt */
            }
            /* Replace existing entry (update) */
            l[i] = *add;
            return 0;
        }
    }
    
    /* Add new entry if space available */
    if (*n >= max) {
        return -1;  /* No space (treating as error) */
    }
    
    l[*n] = *add;
    (*n)++;
    return 0;
}

int identities_remove(hw_identity *l, int n, const char *ident_or_all,
                      const char *invoker, int is_root) {
    int removed = 0;
    int write_pos = 0;
    
    for (int i = 0; i < n; i++) {
        int should_remove = 0;
        
        if (strcmp(ident_or_all, "all") == 0) {
            /* Remove all own identities (always invoker-scoped, even for root) */
            if (strcmp(l[i].user, invoker) == 0) {
                should_remove = 1;
            }
        } else {
            /* Remove specific identity */
            if (strcmp(l[i].ident, ident_or_all) == 0) {
                if (is_root || strcmp(l[i].user, invoker) == 0) {
                    should_remove = 1;
                }
            }
        }
        
        if (should_remove) {
            removed++;
        } else {
            if (write_pos != i) {
                l[write_pos] = l[i];
            }
            write_pos++;
        }
    }
    
    return removed;
}

int identities_save(const char *path, const hw_identity *l, int n) {
    /* Create temp file in same directory for atomic rename */
    char tmppath[512];
    snprintf(tmppath, sizeof(tmppath), "%s.tmp", path);
    
    FILE *f = fopen(tmppath, "w");
    if (!f) return -1;
    
    /* Write each identity */
    for (int i = 0; i < n; i++) {
        const hw_identity *id = &l[i];
        
        /* Type and required fields */
        const char *type_str = (id->type == HW_USB) ? "usb" : "bt";
        fprintf(f, "%s\t%s\t%s", type_str, id->ident, id->user);
        
        /* Optional attributes */
        if (id->verify[0]) fprintf(f, "\tverify=%s", id->verify);
        if (id->mode[0]) fprintf(f, "\tmode=%s", id->mode);
        if (id->pubkey[0]) fprintf(f, "\tpubkey=%s", id->pubkey);
        
        fprintf(f, "\n");
    }
    
    fclose(f);
    
    /* Set permissions to 0644 */
    chmod(tmppath, 0644);
    
    /* Atomic rename */
    if (rename(tmppath, path) != 0) {
        unlink(tmppath);
        return -1;
    }
    
    return 0;
}
