/* Freestanding <string.h> for the render service (graphics plan M0). */
#ifndef MX_FS_STRING_H
#define MX_FS_STRING_H
#include <stddef.h>
void *memcpy(void *restrict d, const void *restrict s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
#endif
