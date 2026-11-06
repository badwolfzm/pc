/* Linkage adapter for pinned upstream Doomgeneric's unmodified w_file.c.
 * stdc_wad_file is upstream's hardcoded default backend symbol; this module
 * supplies it as bounded Warden storage operations rather than host stdio.
 * This intentionally exports no fopen/fread or host filesystem facility.
 */
#include "doom_wad_bridge.h"
static wad_file_t *open_wad(char *name) {
    return warden_doom_wad_class.OpenFile(name);
}
static void close_wad(wad_file_t *f) {
    warden_doom_wad_class.CloseFile(f);
}
static size_t read_wad(wad_file_t *f, unsigned int off, void *dest, size_t size) {
    return warden_doom_wad_class.Read(f, off, dest, size);
}
wad_file_class_t stdc_wad_file = { open_wad, close_wad, read_wad };
