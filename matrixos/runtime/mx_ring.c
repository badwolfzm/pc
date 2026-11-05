#include "mx_ring.h"
#include "../frontend/include/mx_input.h"
#include <string.h>

int mx_ring_init(mx_ring *r, size_t bytes) {
    if (!r || bytes < sizeof *r) return -1;
    memset(r, 0, sizeof *r);
    r->magic = MX_RING_MAGIC; r->version = MX_RING_VERSION;
    r->data_bytes = MX_RING_DATA; r->ndesc = MX_RING_DESCS;
    return 0;
}

mx_ring_take mx_ring_take_batch(mx_ring *r, uint8_t *out, size_t *out_len) {
    uint32_t head = atomic_load_explicit(&r->cmd_head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&r->cmd_tail, memory_order_relaxed);
    if (head == tail) return MX_RING_EMPTY;
    /* Read the descriptor once. Everything in it is untrusted. */
    mx_ring_desc d = r->desc[tail % MX_RING_DESCS];
    uint64_t len = 0;
    bool ok = mx_validate_len(mx_untrusted_u64_new(d.len), d.offset, MX_RING_DATA, &len) == MX_OK
           && len >= sizeof(rcs_batch_hdr) && len <= RCS_MAX_BATCH_BYTES;
    if (ok) {
        memcpy(out, r->data + d.offset, (size_t)len);   /* copy, then check: rcs_submit sees only this copy */
        *out_len = (size_t)len;
        atomic_store_explicit(&r->data_rd, (uint32_t)(d.offset + len), memory_order_release);
    }
    atomic_store_explicit(&r->cmd_tail, tail + 1, memory_order_release);
    return ok ? MX_RING_BATCH : MX_RING_BAD_DESC;
}

void mx_ring_put_result(mx_ring *r, const rcs_result *res) {
    uint32_t head = atomic_load_explicit(&r->res_head, memory_order_relaxed);
    r->res[head % MX_RING_DESCS] = *res;
    atomic_store_explicit(&r->res_head, head + 1, memory_order_release);
}

/* ---- client ---- */

uint32_t mx_ring_in_flight(const mx_ring *r) {
    return atomic_load_explicit(&r->cmd_head, memory_order_relaxed) - atomic_load_explicit(&r->res_tail, memory_order_relaxed);
}

uint8_t *mx_ring_reserve(mx_ring *r, uint32_t len) {
    if (len == 0 || len > RCS_MAX_BATCH_BYTES) return NULL;
    if (atomic_load_explicit(&r->revoked, memory_order_relaxed)) return NULL;
    uint32_t head = atomic_load_explicit(&r->cmd_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&r->cmd_tail, memory_order_acquire);
    uint32_t res_head = atomic_load_explicit(&r->res_head, memory_order_relaxed);
    uint32_t res_tail = atomic_load_explicit(&r->res_tail, memory_order_relaxed);
    if (head - tail >= MX_RING_DESCS || res_head - res_tail >= MX_RING_DESCS) return NULL;  /* descriptor or result table full */
    uint32_t wr = atomic_load_explicit(&r->data_wr, memory_order_relaxed);
    /* Data in use: from the oldest unconsumed batch's offset up to wr. */
    uint32_t oldest = head == tail ? wr : r->desc[tail % MX_RING_DESCS].offset;
    if (head == tail) { wr = 0; oldest = 0; }   /* nothing pending: restart at the bottom */
    uint32_t start;
    if (oldest <= wr) {                           /* [oldest, wr) in use, free is [wr, end) and [0, oldest) */
        if (MX_RING_DATA - wr >= len) start = wr;
        else if (oldest > len) start = 0;         /* strictly: keep start != oldest so the ring never reads as empty while full */
        else return NULL;
    } else {                                      /* [oldest, end) and [0, wr) in use, free is [wr, oldest) */
        if (oldest - wr > len) start = wr;
        else return NULL;
    }
    atomic_store_explicit(&r->data_wr, start, memory_order_relaxed);   /* publish() advances it */
    return r->data + start;
}

void mx_ring_publish(mx_ring *r, const uint8_t *batch, uint32_t len) {
    uint32_t head = atomic_load_explicit(&r->cmd_head, memory_order_relaxed);
    uint32_t off = (uint32_t)(batch - r->data);
    r->desc[head % MX_RING_DESCS] = (mx_ring_desc){ off, len };
    atomic_store_explicit(&r->data_wr, off + len, memory_order_relaxed);
    atomic_store_explicit(&r->cmd_head, head + 1, memory_order_release);
}

bool mx_ring_get_result(mx_ring *r, rcs_result *out) {
    uint32_t head = atomic_load_explicit(&r->res_head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&r->res_tail, memory_order_relaxed);
    if (head == tail) return false;
    *out = r->res[tail % MX_RING_DESCS];
    atomic_store_explicit(&r->res_tail, tail + 1, memory_order_release);
    return true;
}

bool mx_ring_asset_ready(const mx_ring *r, uint32_t slot) {
    if (slot >= RCS_MAX_ASSETS) return false;
    return (atomic_load_explicit(&r->ready_mask[slot / 64], memory_order_acquire) >> (slot % 64)) & 1u;
}
