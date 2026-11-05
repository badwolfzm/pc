/*
 * rcs.h - Warden render service command set (RCS), protocol version 1.
 *
 * This header is the wire contract between a game (client) and the render
 * service container. Everything here is fixed-size, little-endian, and free
 * of pointers, addresses, paths or strings. Pixels are only ever named by a
 * surface slot (a per-client table index) or an asset slot (an index into the
 * client's granted asset table, filled by the policy container from the
 * manifest). Payload bytes appear in exactly one command, UPLOAD, and are
 * bounded by the receiver.
 *
 * Every limit below belongs to the RECEIVER. The sender's own length fields
 * are checked against these, never the other way round.
 *
 * Reference implementation and tests: rcs.c, rcs_test.c.
 */
#ifndef RCS_H
#define RCS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RCS_VERSION 1u

/* ---- receiver-owned limits ------------------------------------------- */
#define RCS_MAX_BATCH_BYTES       (256u * 1024u)   /* one batch, header + commands + payload */
#define RCS_MAX_BATCH_CMDS        4096u
#define RCS_MAX_SURFACES          256u             /* per client */
#define RCS_MAX_ASSETS            256u             /* per client */
#define RCS_MAX_DIM               4096u            /* surface width or height */
#define RCS_MAX_SURFACE_PIXELS    (8u * 1024u * 1024u) /* sum over a client's surfaces: 32 MiB ARGB */
#define RCS_MAX_UPLOAD_BYTES      (64u * 1024u)    /* payload of one UPLOAD */
#define RCS_FRAME_UPLOAD_BUDGET   (2u * 1024u * 1024u) /* UPLOAD bytes per frame */
#define RCS_FRAME_PIXEL_BUDGET    6000000ull       /* 2D pixel-ops per frame (~6.5x 720p overdraw) */
#define RCS_FRAME_BIND_BUDGET     (8u * 1024u * 1024u) /* BIND_ASSET pixel bytes per frame: two 720p backgrounds */
#define RCS_FRAME_TRIANGLE_BUDGET 20000u           /* triangles per frame */
#define RCS_MAX_FENCES_PER_BATCH  16u
#define RCS_SLOT_NONE             0xFFFFFFFFu
#define RCS_TILE                  16u              /* damage tracking granularity, pixels */

/* ---- rights on the render capability --------------------------------- */
enum rcs_right {
    RCS_R_RESOURCE = 1u << 0, /* CREATE_SURFACE, DESTROY_SURFACE */
    RCS_R_ASSET    = 1u << 1, /* BIND_ASSET */
    RCS_R_UPLOAD   = 1u << 2, /* UPLOAD */
    RCS_R_DRAW     = 1u << 3, /* SET_CLIP, CLEAR, FILL_RECT, BLIT, LINE */
    RCS_R_MESH     = 1u << 4, /* DRAW_TRIANGLES */
    RCS_R_PRESENT  = 1u << 5, /* BEGIN_FRAME, PRESENT, FENCE */
    RCS_R_ALL      = 0x3Fu
};

/* ---- opcodes ----------------------------------------------------------- */
enum rcs_opcode {
    /* resource class 0x01..0x0F: only in a RESOURCE batch */
    RCS_OP_CREATE_SURFACE  = 0x01,
    RCS_OP_DESTROY_SURFACE = 0x02,
    RCS_OP_BIND_ASSET      = 0x03,
    RCS_OP_UPLOAD          = 0x04,
    /* draw class 0x10..0x1F: only in a FRAME batch, between BEGIN_FRAME and PRESENT */
    RCS_OP_SET_CLIP        = 0x10,
    RCS_OP_CLEAR           = 0x11,
    RCS_OP_FILL_RECT       = 0x12,
    RCS_OP_BLIT            = 0x13,
    RCS_OP_LINE            = 0x14,
    RCS_OP_DRAW_TRIANGLES  = 0x15,
    /* frame class 0x20..0x2F: only in a FRAME batch */
    RCS_OP_BEGIN_FRAME     = 0x20,
    RCS_OP_PRESENT         = 0x21,
    RCS_OP_FENCE           = 0x22,
    RCS_OP_PROMISE_DROP    = 0x23
};

enum rcs_batch_kind { RCS_BATCH_RESOURCE = 1, RCS_BATCH_FRAME = 2 };
enum rcs_format     { RCS_FMT_ARGB8888 = 0, RCS_FMT_A8 = 1, RCS_FMT_COUNT = 2 };
enum rcs_blend      { RCS_BLEND_COPY = 0, RCS_BLEND_ALPHA = 1, RCS_BLEND_ADD = 2, RCS_BLEND_COUNT = 3 };
enum rcs_shading    { RCS_SHADE_FLAT = 0, RCS_SHADE_BAKED = 1, RCS_SHADE_COUNT = 2 };
enum rcs_asset_kind { RCS_ASSET_NONE = 0, RCS_ASSET_IMAGE = 1, RCS_ASSET_MESH = 2 };

/* ---- wire structures (packed, little-endian) --------------------------- */
#pragma pack(push, 1)

#define RCS_BATCH_MAGIC  0x42534352u /* 'RCSB' */
#define RCS_RESULT_MAGIC 0x52534352u /* 'RCSR' */

typedef struct {
    uint32_t magic;       /* RCS_BATCH_MAGIC */
    uint8_t  version;     /* RCS_VERSION */
    uint8_t  kind;        /* rcs_batch_kind */
    uint16_t reserved;    /* 0 */
    uint32_t cmd_count;   /* <= RCS_MAX_BATCH_CMDS */
    uint32_t total_bytes; /* == bytes delivered by the ring descriptor */
} rcs_batch_hdr;          /* 16 bytes */

typedef struct {
    uint16_t opcode;
    uint8_t  version;     /* RCS_VERSION */
    uint8_t  flags;       /* 0 */
    uint32_t body_len;    /* fixed per opcode; UPLOAD adds its payload */
    uint32_t seq;         /* index of this command in the batch */
    uint32_t reserved;    /* 0 */
} rcs_cmd_hdr;            /* 16 bytes */

typedef struct { int32_t x, y, w, h; } rcs_rect;

typedef struct { uint32_t slot, w, h, format; }                          rcs_create_surface; /* 16 */
typedef struct { uint32_t slot; }                                        rcs_destroy_surface; /* 4 */
typedef struct { uint32_t asset_slot, surface_slot; }                    rcs_bind_asset;      /* 8 */
typedef struct { uint32_t slot; rcs_rect r; /* payload follows */ }      rcs_upload;          /* 20 + w*h*bpp */
typedef struct { uint32_t target; rcs_rect r; }                          rcs_set_clip;        /* 20 */
typedef struct { uint32_t target, color; }                               rcs_clear;           /* 8 */
typedef struct { uint32_t target; rcs_rect r; uint32_t color; }          rcs_fill_rect;       /* 24 */
typedef struct { uint32_t target; int32_t dx, dy; uint32_t src; rcs_rect sr; uint32_t blend, flags; } rcs_blit; /* 40 */
typedef struct { uint32_t target; int32_t x0, y0, x1, y1; uint32_t color; } rcs_line;         /* 24 */
typedef struct { uint32_t target, mesh_slot, first_tri, tri_count, texture, shading; } rcs_draw_triangles; /* 24 */
typedef struct { uint32_t frame_no; }                                    rcs_begin_frame;     /* 4 */
typedef struct { uint32_t target, frame_no; }                            rcs_present;         /* 8 */
typedef struct { uint32_t token; }                                       rcs_fence;           /* 4 */
typedef struct { uint32_t rights_mask; }                                 rcs_promise_drop;    /* 4 */

/* Result record the service writes back for every batch. Nothing is partially
 * applied: on any error the client's state is exactly as before the batch. */
typedef struct {
    uint32_t magic;          /* RCS_RESULT_MAGIC */
    uint32_t status;         /* rcs_status */
    uint32_t failing_index;  /* index of the first rejected command, or 0xFFFFFFFF */
    uint32_t error;          /* rcs_error */
    uint64_t pixel_ops;      /* work charged by this batch (0 if rejected) */
    uint32_t triangles;
    uint32_t upload_bytes;
    uint32_t fences_done;    /* number of FENCE tokens completed (reference: all, synchronously) */
    uint32_t bind_bytes;
} rcs_result;                /* 40 bytes */

#pragma pack(pop)

enum rcs_status { RCS_OK = 0, RCS_REJECTED = 1 };

enum rcs_error {
    RCS_E_NONE = 0,
    /* envelope */
    RCS_E_TOO_LARGE,        /* bytes > RCS_MAX_BATCH_BYTES */
    RCS_E_BAD_MAGIC,
    RCS_E_BAD_VERSION,
    RCS_E_BAD_KIND,
    RCS_E_RESERVED_NONZERO,
    RCS_E_LENGTH_MISMATCH,  /* total_bytes != delivered, or commands do not end exactly at the end */
    RCS_E_TOO_MANY_CMDS,
    RCS_E_BAD_SEQ,
    RCS_E_BAD_OPCODE,
    RCS_E_WRONG_CLASS,      /* resource op in a frame batch or vice versa */
    RCS_E_BAD_BODY_LEN,
    /* authority */
    RCS_E_RIGHT_MISSING,
    /* state */
    RCS_E_NOT_IN_FRAME,
    RCS_E_ALREADY_IN_FRAME,
    RCS_E_FRAME_NO,         /* PRESENT frame_no != BEGIN_FRAME frame_no, or not increasing */
    /* slots */
    RCS_E_SLOT_RANGE,
    RCS_E_SLOT_EMPTY,
    RCS_E_SLOT_OCCUPIED,
    RCS_E_ASSET_KIND,
    RCS_E_SAME_SURFACE,
    /* geometry */
    RCS_E_BAD_DIM,          /* 0 or > RCS_MAX_DIM */
    RCS_E_BAD_FORMAT,
    RCS_E_FORMAT_MISMATCH,
    RCS_E_RECT_EMPTY,       /* w <= 0 or h <= 0 */
    RCS_E_RECT_OUTSIDE,     /* a rect that must lie inside its surface does not (UPLOAD, BLIT source, SET_CLIP) */
    RCS_E_BAD_BLEND,
    RCS_E_BAD_SHADING,
    RCS_E_MESH_RANGE,
    /* budgets */
    RCS_E_SURFACE_BUDGET,
    RCS_E_UPLOAD_BUDGET,
    RCS_E_PIXEL_BUDGET,
    RCS_E_TRIANGLE_BUDGET,
    RCS_E_FENCE_BUDGET,
    RCS_E_BIND_BUDGET,
    RCS_E_COUNT
};

/* ---- service-side model (reference) ------------------------------------ */
typedef struct {
    bool      live;
    uint32_t  w, h, format;
    uint32_t *px;       /* w*h entries; A8 stores one byte per pixel in the low 8 bits */
    uint32_t  tw, th;   /* damage tiles across and down (RCS_TILE pixels each) */
    uint8_t  *tiles;    /* tw*th bits, row-major; set when a draw wrote into the tile since the last PRESENT */
    uint32_t  dirty;    /* number of set tiles */
} rcs_surface;

/* What PRESENT hands to the compositor (service -> canvas, not on the client ring).
 * The bitmap is bounded by the surface: at most (4096/16)^2 bits = 8 KiB, 450 bytes at 720p. */
typedef struct {
    uint32_t       slot, frame_no, tw, th, dirty_tiles;
    const uint8_t *tiles;   /* tw*th bits; valid until the next batch on this client */
} rcs_present_info;

/* An entry in the client's granted asset table. Filled by policy/storage, never by
 * the client. The reference points data at harness memory; the real service pulls
 * from the storage container's read-only asset store. */
typedef struct {
    uint32_t kind;      /* rcs_asset_kind */
    uint32_t w, h, format;      /* IMAGE */
    uint32_t tri_count;         /* MESH */
    const uint32_t *image;      /* IMAGE pixels, w*h */
    const int32_t  *mesh;       /* MESH: tri_count * 7 ints: x0,y0,x1,y1,x2,y2 (16.16 fixed), color */
} rcs_asset;

typedef struct {
    uint32_t    rights;
    rcs_surface surfaces[RCS_MAX_SURFACES];
    rcs_asset   assets[RCS_MAX_ASSETS];
    uint64_t    surface_pixels;   /* sum of w*h over live surfaces */
    bool        in_frame;
    uint32_t    frame_no;         /* of the open frame */
    uint32_t    last_presented;   /* frame_no of the last PRESENT, 0 if none */
    bool        have_presented;
    uint64_t    frame_pixel_ops;  /* charged in the open frame */
    uint32_t    frame_triangles;
    uint32_t    frame_upload;     /* bytes uploaded since last PRESENT */
    uint32_t    frame_bind;       /* asset bytes bound since last PRESENT */
    bool        clip_set;
    uint32_t    clip_target;
    rcs_rect    clip;
    uint64_t    presents;         /* count, for tests */
    rcs_present_info last_present; /* filled by PRESENT; tiles cleared at the next BEGIN_FRAME */
    uint32_t    fence_log[RCS_MAX_FENCES_PER_BATCH];
} rcs_client;

void rcs_client_init(rcs_client *c, uint32_t rights);
void rcs_client_free(rcs_client *c);

/* Validate then execute one batch. `bytes` is the receiver's private copy of
 * what the ring delivered (`n` bytes, as the descriptor said). Returns the
 * result record; on RCS_REJECTED the client is unchanged. */
rcs_result rcs_submit(rcs_client *c, const uint8_t *bytes, size_t n);

/* Fixed body size for an opcode, or 0 if unknown. UPLOAD returns the fixed part. */
uint32_t rcs_body_size(uint16_t opcode);
const char *rcs_error_name(uint32_t e);

/* Deterministic hash of a surface's pixels (FNV-1a), for tests. */
uint64_t rcs_surface_hash(const rcs_client *c, uint32_t slot);

#endif /* RCS_H */
