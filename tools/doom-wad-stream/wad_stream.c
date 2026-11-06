#include "wad_stream.h"
#include <string.h>
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int range_ok(uint32_t size, uint32_t off, uint32_t len) {
    return off <= size && len <= size - off;
}
void wad_close(wad_stream *s) { if (s) memset(s, 0, sizeof(*s)); }
int wad_open(wad_stream *s, void *ctx, wad_read_at read_at, uint32_t bytes) {
    uint8_t header[12];
    if (!s) return 0;
    wad_close(s);
    if (!read_at || bytes < sizeof(header) || bytes > WAD_MAX_SIZE) return 0;
    if (read_at(ctx, 0, header, sizeof(header)) != 1) return 0;
    uint8_t kind;
    if (memcmp(header, "IWAD", 4) == 0) kind = 1;
    else if (memcmp(header, "PWAD", 4) == 0) kind = 2;
    else return 0;
    uint32_t count = le32(header + 4), dir = le32(header + 8);
    if (count > WAD_MAX_LUMPS || !range_ok(bytes, dir, count * 16u)) return 0;
    s->ctx = ctx; s->read_at = read_at; s->size = bytes;
    s->directory_off = dir; s->lump_count = count;
    s->cursor = 0; s->kind = kind; s->opened = 1;
    return 1;
}
int wad_lump_at(wad_stream *s, uint32_t index, wad_lump *out) {
    uint8_t record[16];
    if (!s || !s->opened || !out || index >= s->lump_count) return 0;
    uint32_t pos = s->directory_off + index * 16u;
    if (!range_ok(s->size, pos, 16u) || s->read_at(s->ctx, pos, record, sizeof(record)) != 1) return 0;
    uint32_t off = le32(record), len = le32(record + 4);
    if (!range_ok(s->size, off, len)) return 0;
    wad_lump temp;
    temp.offset = off; temp.size = len;
    memcpy(temp.name, record + 8, 8);
    *out = temp;
    return 1;
}
int wad_seek(wad_stream *s, uint32_t pos) {
    if (!s || !s->opened || pos > s->size) return 0;
    s->cursor = pos;
    return 1;
}
int wad_read(wad_stream *s, void *dst, size_t len) {
    if (!s || !s->opened || (!dst && len != 0) || len > UINT32_MAX ||
        !range_ok(s->size, s->cursor, (uint32_t)len)) return 0;
    if (len == 0) return 1;
    if (s->read_at(s->ctx, s->cursor, dst, len) != 1) return 0;
    s->cursor += (uint32_t)len;
    return 1;
}
