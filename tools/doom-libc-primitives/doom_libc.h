#ifndef WARDEN_DOOM_LIBC_H
#define WARDEN_DOOM_LIBC_H
#include <stddef.h>
void *wd_memcpy(void *dst, const void *src, size_t n);
void *wd_memmove(void *dst, const void *src, size_t n);
void *wd_memset(void *dst, int value, size_t n);
int wd_memcmp(const void *lhs, const void *rhs, size_t n);
size_t wd_strlen(const char *s);
size_t wd_strnlen(const char *s, size_t bound);
#endif
