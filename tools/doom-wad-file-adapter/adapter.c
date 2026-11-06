#include "adapter.h"
int doom_wad_file_open(doom_wad_file *file, void *ctx, wad_read_at read_at, uint32_t size) {
    if (!file) return 0;
    return wad_open(&file->stream, ctx, read_at, size);
}
int doom_wad_file_seek(doom_wad_file *file, int64_t offset, int whence) {
    if (!file || !file->stream.opened) return 0;
    int64_t base;
    switch (whence) {
        case DOOM_WAD_SEEK_SET: base = 0; break;
        case DOOM_WAD_SEEK_CUR: base = (int64_t)file->stream.cursor; break;
        case DOOM_WAD_SEEK_END: base = (int64_t)file->stream.size; break;
        default: return 0;
    }
    /* Avoid signed overflow: the valid result must be in [0, size].
       Check the offset against the permitted interval before adding. */
    if (offset < -base || offset > (int64_t)file->stream.size - base) return 0;
    return wad_seek(&file->stream, (uint32_t)(base + offset));
}
int doom_wad_file_read(doom_wad_file *file, void *dest, size_t count) {
    if (!file) return 0;
    return wad_read(&file->stream, dest, count);
}
int doom_wad_file_tell(const doom_wad_file *file, uint32_t *position) {
    if (!file || !file->stream.opened || !position) return 0;
    *position = file->stream.cursor;
    return 1;
}
void doom_wad_file_close(doom_wad_file *file) {
    if (file) wad_close(&file->stream);
}
