#include "wad.h"
#define WAD_MAX_BYTES (64u * 1024u * 1024u)
#define WAD_MAX_LUMPS 65536u
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int fits(size_t limit, uint32_t offset, uint32_t length) {
    return (uint64_t)offset + (uint64_t)length <= (uint64_t)limit;
}
int wad_open(wad_view *out, const void *bytes, size_t size) {
    if (out == NULL) return -1;
    *out = (wad_view){0};
    if (bytes == NULL || size < 12 || size > WAD_MAX_BYTES) return -1;
    const uint8_t *p = (const uint8_t *)bytes;
    if (!((p[0]=='I' && p[1]=='W') || (p[0]=='P' && p[1]=='W')) ||
        p[2]!='A' || p[3]!='D') return -1;
    uint32_t count = le32(p+4), dir = le32(p+8);
    if (count > WAD_MAX_LUMPS || !fits(size,dir, count*16u)) return -1;
    for (uint32_t i=0; i<count; ++i) {
        const uint8_t *e = p + (size_t)dir + (size_t)i*16u;
        if (!fits(size,le32(e),le32(e+4))) return -1;
    }
    *out=(wad_view){p,size,count,dir};
    return 0;
}
int wad_lump(const wad_view *wad, uint32_t index, const uint8_t **data, uint32_t *size) {
    if (data != NULL) *data=NULL;
    if (size != NULL) *size=0;
    if (wad==NULL || data==NULL || size==NULL || wad->bytes==NULL || index>=wad->count) return -1;
    if (wad->size > WAD_MAX_BYTES || wad->count>WAD_MAX_LUMPS ||
        !fits(wad->size,wad->directory,wad->count*16u)) return -1;
    const uint8_t *e=wad->bytes+(size_t)wad->directory+(size_t)index*16u;
    uint32_t off=le32(e), n=le32(e+4);
    if (!fits(wad->size,off,n)) return -1;
    *data=wad->bytes+off;
    *size=n;
    return 0;
}
int wad_find_last(const wad_view *wad, const char name[8], uint32_t *index) {
    if (index!=NULL) *index=0;
    if (wad==NULL || name==NULL || index==NULL || wad->bytes==NULL ||
        wad->count>WAD_MAX_LUMPS || !fits(wad->size,wad->directory,wad->count*16u)) return -1;
    for (uint32_t j=wad->count; j>0; --j) {
        const uint8_t *e=wad->bytes+(size_t)wad->directory+(size_t)(j-1)*16u+8u;
        unsigned int same=1;
        for (unsigned int k=0;k<8;++k) if (e[k]!=(uint8_t)name[k]) same=0;
        if (same) { *index=j-1; return 0; }
    }
    return -1;
}
