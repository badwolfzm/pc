#ifndef WARDEN_GAME_FRAME_PROBE_H
#define WARDEN_GAME_FRAME_PROBE_H
#define PROBE_WIDTH 320U
#define PROBE_HEIGHT 200U
/* One frame = 256000 bytes at ARGB8888; caller owns the buffer.
 * render() touches exactly that region; no dynamic memory or syscalls. */
void probe_render(unsigned int frame, unsigned int pixels[PROBE_WIDTH * PROBE_HEIGHT]);
unsigned long long probe_hash(const unsigned int pixels[PROBE_WIDTH * PROBE_HEIGHT]);
#endif
