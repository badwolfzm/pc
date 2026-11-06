#ifndef WARDEN_WAD_STREAM_H
#define WARDEN_WAD_STREAM_H
#include <stddef.h>
#include <stdint.h>
typedef int (*wad_read_at)(void *ctx, uint32_t offset, void *dst, size_t len);
typedef struct {
    void *ctx;
    wad_read_at read_at;
    uint32_t size;
    uint32_t directory_off;
    uint32_t lump_count;
    uint32_t cursor;
    uint8_t kind;
    uint8_t opened;
} wad_stream;
typedef struct {
    uint32_t offset;
    uint32_t size;
    char name[8];
} wad_lump;
#define WAD_MAX_SIZE (64u * 1024u * 1024u)
#define WAD_MAX_LUMPS 65536u
int wad_open(wad_stream *s, void *ctx, wad_read_at read_at, uint32_t bytes);
int wad_lump_at(wad_stream *s, uint32_t index, wad_lump *out);
int wad_seek(wad_stream *s, uint32_t pos);
int wad_read(wad_stream *s, void *dst, size_t len);
void wad_close(wad_stream *s);
#endif
