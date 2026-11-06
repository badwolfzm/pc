#ifndef WARDEN_DOOM_MEMORY_H
#define WARDEN_DOOM_MEMORY_H
#include <stddef.h>
void *doom_memcpy(void *restrict dst, const void *restrict src, size_t n);
void *doom_memmove(void *dst, const void *src, size_t n);
void *doom_memset(void *dst, int value, size_t n);
int doom_memcmp(const void *a, const void *b, size_t n);
#endif
