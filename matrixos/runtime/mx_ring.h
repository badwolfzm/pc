/*
 * mx_ring.h - command/result ring between a game and the render service.
 *
 * Host prototype of the shared-memory ring that gpu-commands.md section 3
 * recommends: a 1 MiB command data area, a table of fixed-size descriptors
 * (offset, length), and a result table of rcs_result records. One ring pair
 * per client. The service creates it; the client only maps it.
 *
 * Trust model (the whole point of this file):
 *   - The client writes: cmd_head, data_wr, desc[] and data[] bytes, res_tail.
 *   - The service writes: cmd_tail, data_rd, res_head, res[], ready_mask[].
 *   - Nothing the client writes is believed. The service reads each shared
 *     field exactly once into a local, checks it against sizes IT owns
 *     (mx_validate_len from the typed-input header, review finding 6), copies
 *     the batch bytes out of the ring, and only then hands the private copy
 *     to rcs_submit(). The client cannot change a batch between check and use.
 *   - A client that corrupts its own ring can only make its own batches fail.
 *
 * On the Warden kernel the same layout lives in pages mapped into both
 * address spaces; "a descriptor was published" is an IPC send on the client's
 * endpoint capability (one register word: the descriptor index), and the
 * ring capability's rights/budgets are the render permission's attributes.
 * On the host the service polls the head index (mx_ring_pump in mx_render.c).
 */
#ifndef MX_RING_H
#define MX_RING_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "../contracts/gpu-commands-ref/rcs.h"

#define MX_RING_MAGIC     0x474E5258u   /* 'XRNG' */
#define MX_RING_VERSION   1u
#define MX_RING_DATA      (1024u * 1024u)   /* command data area, section 3 recommendation */
#define MX_RING_DESCS     64u               /* in-flight batches and result records */

typedef struct { uint32_t offset, len; } mx_ring_desc;

typedef struct {
    uint32_t magic, version, data_bytes, ndesc;
    /* producer side (client-written) */
    _Atomic uint32_t cmd_head;    /* descriptors published */
    _Atomic uint32_t data_wr;     /* client's write cursor, informational */
    _Atomic uint32_t res_tail;    /* results the client has consumed */
    /* consumer side (service-written) */
    _Atomic uint32_t cmd_tail;    /* descriptors consumed */
    _Atomic uint32_t data_rd;     /* end of the last consumed batch; the client frees up to here */
    _Atomic uint32_t res_head;    /* results written */
    _Atomic uint32_t revoked;     /* 1: the service tore the client down; nothing more is accepted */
    _Atomic uint64_t ready_mask[(RCS_MAX_ASSETS + 63) / 64];  /* asset slot i resident and bound-able */
    mx_ring_desc desc[MX_RING_DESCS];
    rcs_result   res[MX_RING_DESCS];
    uint8_t      data[MX_RING_DATA];
} mx_ring;

/* Service side ----------------------------------------------------------- */

/* Format a mapping as an empty ring. `bytes` must be >= sizeof(mx_ring). */
int  mx_ring_init(mx_ring *r, size_t bytes);

typedef enum {
    MX_RING_EMPTY = 0,        /* nothing published */
    MX_RING_BATCH = 1,        /* a batch was copied into out; *out_len set */
    MX_RING_BAD_DESC = 2,     /* descriptor outside the data area or over RCS_MAX_BATCH_BYTES; consumed, reported */
} mx_ring_take;

/* Take the next published descriptor. Copies at most RCS_MAX_BATCH_BYTES into
 * `out` (which must hold that many). The descriptor is consumed either way, so
 * a bad descriptor cannot wedge the ring; the caller writes a rejection result. */
mx_ring_take mx_ring_take_batch(mx_ring *r, uint8_t *out, size_t *out_len);

/* Publish a result record for the batch just taken. */
void mx_ring_put_result(mx_ring *r, const rcs_result *res);

/* Client side ------------------------------------------------------------- */

/* Reserve `len` contiguous bytes in the data area. Returns a pointer to write
 * the batch into, or NULL when the ring is full (results not yet consumed or
 * data not yet freed). The client owns this bookkeeping; the service verifies. */
uint8_t *mx_ring_reserve(mx_ring *r, uint32_t len);
/* Publish the batch reserved last. */
void mx_ring_publish(mx_ring *r, const uint8_t *batch, uint32_t len);
/* Pop the next result, if any. */
bool mx_ring_get_result(mx_ring *r, rcs_result *out);
/* Number of batches published but not yet answered. */
uint32_t mx_ring_in_flight(const mx_ring *r);
/* Resident asset slots (service-written). */
bool mx_ring_asset_ready(const mx_ring *r, uint32_t slot);

#endif
