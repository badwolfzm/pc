#ifndef DOOM_WAD_FILE_ADAPTER_H
#define DOOM_WAD_FILE_ADAPTER_H
#include <stddef.h>
#include <stdint.h>
#include "../doom-wad-stream/wad_stream.h"
typedef struct { wad_stream stream; } doom_wad_file;
enum { DOOM_WAD_SEEK_SET = 0, DOOM_WAD_SEEK_CUR = 1, DOOM_WAD_SEEK_END = 2 };
/* Open requires a previously authorized and immutable WAD byte-source. */
int doom_wad_file_open(doom_wad_file *file, void *ctx, wad_read_at read_at, uint32_t size);
int doom_wad_file_seek(doom_wad_file *file, int64_t offset, int whence);
int doom_wad_file_read(doom_wad_file *file, void *dest, size_t count);
/* A successful read transfers exactly count bytes. No implicit partial reads. */
int doom_wad_file_tell(const doom_wad_file *file, uint32_t *position);
void doom_wad_file_close(doom_wad_file *file);
#endif
