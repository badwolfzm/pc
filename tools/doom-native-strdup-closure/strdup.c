#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Genuine freestanding implementation; standard input must be NUL terminated. */
char *strdup(const char *source) {
    if (source == NULL) return NULL;
    size_t n = strlen(source);
    if (n == SIZE_MAX) return NULL;
    char *result = malloc(n + 1u);
    if (result == NULL) return NULL;
    memcpy(result, source, n + 1u);
    return result;
}
