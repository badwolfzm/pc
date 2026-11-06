#ifndef WARDEN_GP_TRANSPORT_H
#define WARDEN_GP_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>
#define GT_WIDTH 160u
#define GT_HEIGHT 100u
#define GT_PIXELS (GT_WIDTH * GT_HEIGHT)
#define GT_TILE 16u
#define GT_TILE_COLS ((GT_WIDTH + GT_TILE - 1u) / GT_TILE)
#define GT_TILE_ROWS ((GT_HEIGHT + GT_TILE - 1u) / GT_TILE)
#define GT_TILE_COUNT (GT_TILE_COLS * GT_TILE_ROWS)
#define GT_QUEUE 32u
#define GT_OK 0
#define GT_ERR -1
#define GT_FULL -2
#define GT_EMPTY -3
#define GT_KEY_LEFT 1u
#define GT_KEY_RIGHT 2u
#define GT_KEY_UP 4u
#define GT_KEY_DOWN 8u
/* Buffers and state are caller-owned. No dynamic memory allocation. */
typedef struct { uint8_t key; uint8_t down; } gt_event;
typedef struct { gt_event events[GT_QUEUE]; uint32_t read, count; } gt_input;
typedef struct { uint32_t pixels[GT_PIXELS]; uint64_t hashes[GT_TILE_COUNT]; uint64_t frame; uint8_t valid; } gt_surface;
int gt_input_push(gt_input *q, gt_event event);
int gt_input_pop(gt_input *q, gt_event *out);
int gt_present(gt_surface *s, const uint32_t *src, size_t bytes, uint32_t width,
               uint32_t height, uint32_t stride_pixels, uint32_t *changed);
#endif
