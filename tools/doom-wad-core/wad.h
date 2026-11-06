#ifndef WARDEN_WAD_CORE_H
#define WARDEN_WAD_CORE_H
#include <stddef.h>
#include <stdint.h>
/* A borrowed immutable memory buffer, not an I/O or permissions grant. */
typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint32_t count;
    uint32_t directory;
} wad_view;
/* 0 success, negative means untrusted/malformed input or absent lump. */
int wad_open(wad_view *out, const void *bytes, size_t size);
int wad_lump(const wad_view *wad, uint32_t index, const uint8_t **data, uint32_t *size);
/* Doom lump names are 8 raw bytes, zero padded. Last matching entry wins. */
int wad_find_last(const wad_view *wad, const char name[8], uint32_t *index);
#endif
