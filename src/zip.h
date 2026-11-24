#ifndef NES_ZIP_H
#define NES_ZIP_H

#include <stddef.h>
#include <stdint.h>

/* True if buf starts with a zip local file header ("PK\3\4"). */
int zip_is_zip(const uint8_t *buf, size_t len);

/* Extracts the ROM from an in-memory zip archive: the first entry whose name
 * ends in ".nes" (case-insensitive), skipping directories and macOS metadata
 * (__MACOSX/, ._*); if there is none but the archive holds exactly one file,
 * that file. Only stored and deflated entries are supported.
 * On success returns 0 and sets *out (malloc'd, caller frees), *out_len and
 * *name (points into buf, not NUL-terminated, length *name_len). On failure
 * prints "<label>: reason" to stderr and returns -1. */
int zip_extract_rom(const uint8_t *buf, size_t len, const char *label,
                    uint8_t **out, size_t *out_len,
                    const char **name, size_t *name_len);

#endif
