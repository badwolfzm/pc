/* Minimal test-only declaration of Doomgeneric's platform ABI.
 * The actual engine's original header must be used for a real Doom build.
 */
#ifndef WARDEN_DOOMGENERIC_TEST_HEADER
#define WARDEN_DOOMGENERIC_TEST_HEADER
#include <stdint.h>
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200
typedef uint32_t pixel_t;
extern pixel_t *DG_ScreenBuffer;
void DG_Init(void);
void DG_DrawFrame(void);
void DG_SleepMs(uint32_t);
uint32_t DG_GetTicksMs(void);
int DG_GetKey(int *, unsigned char *);
void DG_SetWindowTitle(const char *);
#endif
