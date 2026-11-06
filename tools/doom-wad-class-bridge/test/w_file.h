/* Test-only structural declarations of Doomgeneric w_file.h's public ABI.
   The real upstream header is required for eventual source-port integration. */
#ifndef TEST_W_FILE_H
#define TEST_W_FILE_H
#include <stddef.h>
#include <stdint.h>
typedef unsigned char byte;
typedef struct _wad_file_s wad_file_t;
typedef struct {
    wad_file_t *(*OpenFile)(char *path);
    void (*CloseFile)(wad_file_t *file);
    size_t (*Read)(wad_file_t *file, unsigned int offset,
                   void *buffer, size_t buffer_len);
} wad_file_class_t;
struct _wad_file_s {
    wad_file_class_t *file_class;
    byte *mapped;
    unsigned int length;
};
#endif
