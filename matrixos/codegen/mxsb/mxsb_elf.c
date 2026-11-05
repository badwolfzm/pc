/*
 * ELF64 -> MXSB image extraction.
 *
 * Layout convention (the contract's section table carries no addresses, so
 * the emitter and the loader agree on this and nothing else):
 *
 *   base       = page_floor(lowest PT_LOAD vaddr)
 *   TEXT image = [base, data_start)            mapped at B (R+X)
 *   DATA image = [data_start, data_end)        mapped at B + TEXT.size (R+W)
 *   BSS        = memsz tail of the RW segment  zeroed at B + TEXT.size + DATA.size
 *   entry_rel  = e_entry - base
 *
 * where data_start = page_floor(first writable PT_LOAD vaddr).  Because
 * data_start is page aligned and base is page aligned, TEXT.size is a
 * multiple of the page size whenever DATA exists, so "DATA follows TEXT" in
 * the loader reproduces the ELF's own address layout exactly, which is what
 * RIP-relative code needs.  Read-only segments (rodata, RELRO) land inside
 * TEXT and are therefore mapped R+X; they are never writable.
 *
 * Refused: W+X segments, PT_INTERP (dynamically linked), any dynamic
 * relocation (DT_RELASZ/DT_RELSZ/DT_PLTRELSZ != 0): the loader has no
 * relocation step, so only static, relocation-free images are admitted.
 * ET_DYN images set the PIE flag (position independent, no relocs).
 */
#include "mxsb_elf.h"
#include <stdlib.h>
#include <string.h>

#define EI_NIDENT 16
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define DT_PLTRELSZ 2
#define DT_RELASZ 8
#define DT_RELSZ 18
#define DT_NULL 0

static bool rd_ok(size_t len, uint64_t off, uint64_t w) { return off <= len && w <= len - off; }
static uint16_t g16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t g64(const uint8_t *p) { uint64_t x = 0; for (int i = 7; i >= 0; --i) x = (x << 8) | p[i]; return x; }
static uint64_t pfloor(uint64_t x) { return x & ~(uint64_t)(MXSB_ELF_PAGE - 1); }
static uint64_t pceil(uint64_t x) { return (x + MXSB_ELF_PAGE - 1) & ~(uint64_t)(MXSB_ELF_PAGE - 1); }

typedef struct { uint32_t type, flags; uint64_t off, vaddr, filesz, memsz; } ph;

static int cmp_ph(const void *a, const void *b) { const ph *x = a, *y = b; return (x->vaddr > y->vaddr) - (x->vaddr < y->vaddr); }

static bool copy_segment(uint8_t *img, uint64_t img_base, uint64_t img_size, const uint8_t *buf, size_t len, const ph *p, char *err, size_t errlen) {
    if (p->vaddr < img_base || p->vaddr - img_base > img_size || p->filesz > img_size - (p->vaddr - img_base)) {
        snprintf(err, errlen, "segment vaddr 0x%llx outside image", (unsigned long long)p->vaddr); return false;
    }
    if (!rd_ok(len, p->off, p->filesz)) { snprintf(err, errlen, "segment file range out of bounds"); return false; }
    memcpy(img + (p->vaddr - img_base), buf + p->off, (size_t)p->filesz);
    return true;
}

bool mxsb_elf_extract(const uint8_t *buf, size_t len, mxsb_elf_out *o, char *err, size_t errlen) {
    memset(o, 0, sizeof *o);
    if (len < 64 || memcmp(buf, "\x7f" "ELF", 4) != 0) { snprintf(err, errlen, "not an ELF file"); return false; }
    if (buf[4] != 2) { snprintf(err, errlen, "not ELF64"); return false; }
    if (buf[5] != 1) { snprintf(err, errlen, "not little-endian"); return false; }
    uint16_t e_type = g16(buf + 16), e_machine = g16(buf + 18);
    if (e_machine != EM_X86_64) { snprintf(err, errlen, "e_machine %u is not x86-64", e_machine); return false; }
    if (e_type != ET_EXEC && e_type != ET_DYN) { snprintf(err, errlen, "e_type %u is not EXEC or DYN", e_type); return false; }
    uint64_t e_entry = g64(buf + 24), e_phoff = g64(buf + 32);
    uint16_t e_phentsize = g16(buf + 54), e_phnum = g16(buf + 56);
    if (e_phentsize != 56 || e_phnum == 0 || e_phnum > 512) { snprintf(err, errlen, "bad program header table"); return false; }
    uint64_t phbytes = (uint64_t)e_phentsize * e_phnum;
    if (!rd_ok(len, e_phoff, phbytes)) { snprintf(err, errlen, "program headers out of bounds"); return false; }

    ph loads[512]; int nload = 0; ph dyn = {0}; bool have_dyn = false;
    for (unsigned i = 0; i < e_phnum; ++i) {
        const uint8_t *p = buf + e_phoff + (uint64_t)i * 56;
        ph h = { g32(p), g32(p + 4), g64(p + 8), g64(p + 16), g64(p + 32), g64(p + 40) };
        if (h.type == PT_INTERP) { snprintf(err, errlen, "PT_INTERP present: dynamically linked binaries are not admitted"); return false; }
        if (h.type == PT_DYNAMIC) { dyn = h; have_dyn = true; }
        if (h.type != PT_LOAD) continue;
        if ((h.flags & PF_W) && (h.flags & PF_X)) { snprintf(err, errlen, "W+X segment refused"); return false; }
        if (h.memsz < h.filesz) { snprintf(err, errlen, "segment memsz < filesz"); return false; }
        if (!rd_ok(len, h.off, h.filesz)) { snprintf(err, errlen, "PT_LOAD file range out of bounds"); return false; }
        if (h.vaddr > UINT64_MAX - h.memsz) { snprintf(err, errlen, "segment address overflow"); return false; }
        if (h.memsz == 0) continue;
        loads[nload++] = h;
    }
    if (nload == 0) { snprintf(err, errlen, "no PT_LOAD segments"); return false; }
    if (have_dyn) {
        if (!rd_ok(len, dyn.off, dyn.filesz)) { snprintf(err, errlen, "PT_DYNAMIC out of bounds"); return false; }
        for (uint64_t k = 0; k + 16 <= dyn.filesz; k += 16) {
            uint64_t tag = g64(buf + dyn.off + k), val = g64(buf + dyn.off + k + 8);
            if (tag == DT_NULL) break;
            if ((tag == DT_RELASZ || tag == DT_RELSZ || tag == DT_PLTRELSZ) && val != 0) {
                snprintf(err, errlen, "dynamic relocations present (DT tag %llu, %llu bytes): link with -static and avoid address constants in data, or wait for a relocation section in the contract", (unsigned long long)tag, (unsigned long long)val);
                return false;
            }
        }
    }
    qsort(loads, (size_t)nload, sizeof loads[0], cmp_ph);
    uint64_t base = pfloor(loads[0].vaddr);
    /* first writable segment index */
    int wi = -1;
    for (int i = 0; i < nload; ++i) if (loads[i].flags & PF_W) { wi = i; break; }
    for (int i = (wi < 0 ? nload : wi); i < nload; ++i) if (!(loads[i].flags & PF_W)) { snprintf(err, errlen, "non-writable segment after the writable one: unsupported layout"); return false; }
    uint64_t text_end = 0, data_start = 0, data_end = 0, mem_end = 0;
    for (int i = 0; i < (wi < 0 ? nload : wi); ++i) { uint64_t e = loads[i].vaddr + loads[i].filesz; if (e > text_end) text_end = e; uint64_t m = loads[i].vaddr + loads[i].memsz; if (m > text_end) text_end = m; }
    if (wi >= 0) {
        data_start = pfloor(loads[wi].vaddr);
        if (data_start < text_end) { snprintf(err, errlen, "writable segment shares a page with code/rodata (use -Wl,-z,separate-code)"); return false; }
        for (int i = wi; i < nload; ++i) {
            uint64_t fe = loads[i].vaddr + loads[i].filesz, me = loads[i].vaddr + loads[i].memsz;
            if (fe > data_end) data_end = fe;
            if (me > mem_end) mem_end = me;
        }
        if (data_end < data_start) data_end = data_start;
        o->text_size = data_start - base;
    } else {
        o->text_size = pceil(text_end) - base;
    }
    if (o->text_size == 0 || o->text_size > MXSB_ELF_MAX_IMAGE) { snprintf(err, errlen, "TEXT size %llu out of range", (unsigned long long)o->text_size); return false; }
    o->text = calloc(1, (size_t)o->text_size);
    if (!o->text) { snprintf(err, errlen, "out of memory"); return false; }
    for (int i = 0; i < (wi < 0 ? nload : wi); ++i)
        if (!copy_segment(o->text, base, o->text_size, buf, len, &loads[i], err, errlen)) { mxsb_elf_free(o); return false; }
    if (wi >= 0) {
        o->data_size = data_end - data_start;
        if (o->data_size > MXSB_ELF_MAX_IMAGE) { snprintf(err, errlen, "DATA too large"); mxsb_elf_free(o); return false; }
        if (o->data_size) {
            o->data = calloc(1, (size_t)o->data_size);
            if (!o->data) { snprintf(err, errlen, "out of memory"); mxsb_elf_free(o); return false; }
            for (int i = wi; i < nload; ++i)
                if (!copy_segment(o->data, data_start, o->data_size, buf, len, &loads[i], err, errlen)) { mxsb_elf_free(o); return false; }
        }
        o->bss_size = mem_end > data_end ? mem_end - data_end : 0;
    }
    if (e_entry < base || e_entry - base >= o->text_size) { snprintf(err, errlen, "entry 0x%llx outside TEXT", (unsigned long long)e_entry); mxsb_elf_free(o); return false; }
    o->entry_rel = e_entry - base;
    o->load_base = base;
    o->pie = (e_type == ET_DYN);
    return true;
}

void mxsb_elf_free(mxsb_elf_out *o) { free(o->text); free(o->data); memset(o, 0, sizeof *o); }
