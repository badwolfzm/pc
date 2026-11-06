#ifndef WARDEN_DOOM_WAD_BRIDGE_H
#define WARDEN_DOOM_WAD_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#include "wad_stream.h"
/* Doom's w_file.h must be supplied by the engine owner at integration. */
#include "w_file.h"

/* The provider must have already verified source identity, permissions,
   integrity, and size. Only this exact case-sensitive path is opened. */
int warden_doom_wad_grant(const char *path, void *ctx, wad_read_at read_at,
                         uint32_t source_bytes);
void warden_doom_wad_revoke(void);
extern wad_file_class_t warden_doom_wad_class;
#endif
