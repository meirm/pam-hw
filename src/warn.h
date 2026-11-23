/*
 * warn.h — Parser warning callback shared by the config and identities loaders
 */
#ifndef WARN_H
#define WARN_H

/*
 * Called once per rejected or ignored input line. line_no is 1-based;
 * reason is a short static description. The frontend decides where the
 * warning goes (pam_syslog in the module, stderr in pamhw-probe).
 */
typedef void (*pamhw_warn_fn)(void *ctx, int line_no, const char *reason);

#endif /* WARN_H */
