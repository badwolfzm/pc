#ifndef MXSB_ELF_H
#define MXSB_ELF_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#define MXSB_ELF_PAGE 4096u
#define MXSB_ELF_MAX_IMAGE (1ull << 31)
typedef struct {
    uint8_t *text; uint64_t text_size;
    uint8_t *data; uint64_t data_size;
    uint64_t bss_size;
    uint64_t entry_rel;     /* relative to TEXT start */
    uint64_t load_base;     /* ELF base address (informational; 0 for PIE) */
    bool pie;
} mxsb_elf_out;
bool mxsb_elf_extract(const uint8_t *buf, size_t len, mxsb_elf_out *o, char *err, size_t errlen);
void mxsb_elf_free(mxsb_elf_out *o);
#endif
