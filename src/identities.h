/*
 * identities.h — Hardware identity parser and CRUD API
 */
#ifndef IDENTITIES_H
#define IDENTITIES_H

#include "warn.h"

/* Hardware types */
typedef enum {
    HW_BT,
    HW_USB
} hw_type;

/* Hardware identity record */
typedef struct {
    hw_type type;
    char ident[128];         /* Normalized identifier (vid:pid[:serial] or BT MAC) */
    char user[64];           /* Username */
    char verify[16];         /* Optional: verification method */
    char mode[16];           /* Optional: mode */
    char pubkey[256];        /* Optional: public key */
    int line_no;             /* Source line number (0 if not from file) */
} hw_identity;

/*
 * Load identities from file.
 * Returns 0 on success (missing file ⇒ count=0, still returns 0).
 * Malformed lines are skipped (line_no recorded), comments and blanks ignored.
 * Each skipped malformed line is reported through warn (NULL tolerated)
 * with its line number and a reason.
 * count: set to number of loaded identities.
 */
int identities_load(const char *path, hw_identity *out, int max, int *count,
                    pamhw_warn_fn warn, void *warn_ctx);

/*
 * Normalize hardware identifier.
 * USB: lowercase vid:pid[:serial]
 * BT: uppercase AA:BB:CC:DD:EE:FF
 * Returns 0 on valid, -1 on malformed.
 */
int identities_normalize(hw_type t, const char *raw, char *out, int max);

/*
 * Find identity by username.
 * Returns pointer to first match, or NULL if not found.
 */
const hw_identity *identities_find_user(const hw_identity *l, int n, const char *user);

/*
 * Find identity by device identifier (exact match on normalized ident).
 * Returns pointer to first match, or NULL if not found.
 */
const hw_identity *identities_find_device(const hw_identity *l, int n, hw_type t, const char *ident);

/*
 * Add or update identity.
 * Returns:
 *   0: success (added or replaced own existing entry)
 *  -1: hijack attempt (ident owned by other user, caller not root)
 *  -2: not own username (add->user != invoker, caller not root)
 */
int identities_add(hw_identity *l, int *n, int max, const hw_identity *add,
                   const char *invoker, int is_root);

/*
 * Remove identity(-ies).
 * ident_or_all: specific ident or "all" to remove all caller's identities.
 * Returns: number of identities removed.
 * Non-root users can only remove their own identities.
 */
int identities_remove(hw_identity *l, int n, const char *ident_or_all,
                      const char *invoker, int is_root);

/*
 * Save identities to file.
 * Format: type<TAB>ident<TAB>user[<TAB>attr=value]...
 * Atomic write (temp + rename), permissions 0644.
 * Returns 0 on success, -1 on error.
 */
int identities_save(const char *path, const hw_identity *l, int n);

#endif /* IDENTITIES_H */
