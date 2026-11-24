/*
 * ascii.h — ASCII-only character operations
 *
 * Identity lines are stored by the probe (C locale) and matched by the
 * module at authentication time — inside host processes (login, gdm, sudo)
 * that set their own locale. ctype.h macros fold and classify according to
 * LC_CTYPE (glibc's tr_TR: tolower('I') != 'i'), so a stored line could
 * never byte-match a scan from another locale. Folding and classification
 * of identifiers, usernames and serials must never depend on the locale:
 * these helpers are pure ASCII-range operations.
 */
#ifndef PAMHW_ASCII_H
#define PAMHW_ASCII_H

/* Fold ASCII A-Z to a-z; every other byte unchanged. */
static inline int hw_ascii_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Fold ASCII a-z to A-Z; every other byte unchanged. */
static inline int hw_ascii_upper(int c) {
    return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c;
}

/* Printable non-space ASCII exactly (0x21..0x7E). */
static inline int hw_ascii_isgraph(int c) {
    return c >= 0x21 && c <= 0x7E;
}

#endif /* PAMHW_ASCII_H */
