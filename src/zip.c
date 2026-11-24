/* Minimal zip reader: enough to pull one ROM out of an archive. Walks the
 * central directory (local headers may carry zero sizes when a data
 * descriptor is used) and inflates with zlib. No zip64, no encryption. */
#include "zip.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ZIP_MAX_ROM (8u * 1024 * 1024)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int zip_is_zip(const uint8_t *buf, size_t len) {
    return len >= 4 && memcmp(buf, "PK\x03\x04", 4) == 0;
}

static int ends_with_nes(const uint8_t *name, size_t n) {
    if (n < 4) return 0;
    const uint8_t *e = name + n - 4;
    return e[0] == '.' && tolower(e[1]) == 'n' && tolower(e[2]) == 'e' && tolower(e[3]) == 's';
}

/* Directories and macOS resource-fork junk that must never be picked. */
static int is_skipped(const uint8_t *name, size_t n) {
    if (n == 0 || name[n - 1] == '/') return 1;
    if (n >= 9 && memcmp(name, "__MACOSX/", 9) == 0) return 1;
    size_t base = n;
    while (base > 0 && name[base - 1] != '/') base--;
    return n - base >= 2 && name[base] == '.' && name[base + 1] == '_';
}

int zip_extract_rom(const uint8_t *buf, size_t len, const char *label,
                    uint8_t **out, size_t *out_len,
                    const char **name_out, size_t *name_len_out) {
    /* End of central directory: 22 bytes plus up to 64 KiB of comment. */
    if (len < 22) { fprintf(stderr, "%s: truncated zip\n", label); return -1; }
    size_t eocd = (size_t)-1;
    size_t lo = len > 22 + 0xFFFF ? len - 22 - 0xFFFF : 0;
    for (size_t i = len - 22 + 1; i-- > lo;) {
        if (memcmp(buf + i, "PK\x05\x06", 4) == 0) { eocd = i; break; }
    }
    if (eocd == (size_t)-1) { fprintf(stderr, "%s: zip central directory not found\n", label); return -1; }

    uint16_t count = rd16(buf + eocd + 10);
    uint32_t cd_off = rd32(buf + eocd + 16);
    if (count == 0xFFFF || cd_off == 0xFFFFFFFF) {
        fprintf(stderr, "%s: zip64 archives are not supported\n", label);
        return -1;
    }

    /* Pass over the central directory: pick the first .nes, else the only file. */
    const uint8_t *pick = NULL, *only = NULL;
    int files = 0;
    size_t p = cd_off;
    for (uint16_t i = 0; i < count; i++) {
        if (p > len || len - p < 46 || memcmp(buf + p, "PK\x01\x02", 4) != 0) {
            fprintf(stderr, "%s: corrupt zip central directory\n", label);
            return -1;
        }
        size_t nlen = rd16(buf + p + 28), xlen = rd16(buf + p + 30), clen = rd16(buf + p + 32);
        if (len - p - 46 < nlen + xlen + clen) {
            fprintf(stderr, "%s: corrupt zip central directory\n", label);
            return -1;
        }
        const uint8_t *nm = buf + p + 46;
        if (!is_skipped(nm, nlen)) {
            files++;
            only = buf + p;
            if (!pick && ends_with_nes(nm, nlen)) pick = buf + p;
        }
        p += 46 + nlen + xlen + clen;
    }
    if (!pick && files == 1) pick = only;
    if (!pick) { fprintf(stderr, "%s: no .nes file in zip\n", label); return -1; }

    uint16_t flags = rd16(pick + 8), method = rd16(pick + 10);
    uint32_t crc = rd32(pick + 16), csize = rd32(pick + 20), usize = rd32(pick + 24);
    uint32_t loff = rd32(pick + 42);
    size_t nlen = rd16(pick + 28);
    const char *name = (const char *)pick + 46;

    if (flags & 1) { fprintf(stderr, "%s:%.*s: encrypted zip entries are not supported\n", label, (int)nlen, name); return -1; }
    if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF || loff == 0xFFFFFFFF) {
        fprintf(stderr, "%s: zip64 archives are not supported\n", label);
        return -1;
    }
    if (method != 0 && method != 8) {
        fprintf(stderr, "%s:%.*s: zip compression method %u is not supported\n", label, (int)nlen, name, method);
        return -1;
    }
    if (usize == 0 || usize > ZIP_MAX_ROM) {
        fprintf(stderr, "%s:%.*s: implausible ROM size %u\n", label, (int)nlen, name, usize);
        return -1;
    }

    /* Local header's own name/extra lengths decide where the data starts. */
    if (loff > len || len - loff < 30 || memcmp(buf + loff, "PK\x03\x04", 4) != 0) {
        fprintf(stderr, "%s: corrupt zip local header\n", label);
        return -1;
    }
    size_t data = (size_t)loff + 30 + rd16(buf + loff + 26) + rd16(buf + loff + 28);
    if (data > len || len - data < csize) {
        fprintf(stderr, "%s:%.*s: truncated zip entry\n", label, (int)nlen, name);
        return -1;
    }

    uint8_t *rom = malloc(usize);
    if (!rom) { fprintf(stderr, "%s: out of memory\n", label); return -1; }

    if (method == 0) {
        if (csize != usize) {
            fprintf(stderr, "%s:%.*s: bad stored entry size\n", label, (int)nlen, name);
            free(rom);
            return -1;
        }
        memcpy(rom, buf + data, usize);
    } else {
        z_stream s;
        memset(&s, 0, sizeof s);
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK) {
            fprintf(stderr, "%s: inflateInit failed\n", label);
            free(rom);
            return -1;
        }
        s.next_in = (Bytef *)(buf + data);
        s.avail_in = csize;
        s.next_out = rom;
        s.avail_out = usize;
        int r = inflate(&s, Z_FINISH);
        size_t got = s.total_out;
        inflateEnd(&s);
        if (r != Z_STREAM_END || got != usize) {
            fprintf(stderr, "%s:%.*s: corrupt deflate data\n", label, (int)nlen, name);
            free(rom);
            return -1;
        }
    }

    if (crc32(crc32(0, Z_NULL, 0), rom, usize) != crc) {
        fprintf(stderr, "%s:%.*s: CRC mismatch\n", label, (int)nlen, name);
        free(rom);
        return -1;
    }

    *out = rom;
    *out_len = usize;
    *name_out = name;
    *name_len_out = nlen;
    return 0;
}
