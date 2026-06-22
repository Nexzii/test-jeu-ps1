#include "text.h"
#include <psxgpu.h>

void text_init(void) {
    // Font is uploaded to VRAM via FntLoad() in main.c
}

void text_draw(const char *str, int x, int y, uint32_t *ot, char **pri) {
    // FntSort renders each glyph as an 8x8 sprite at (x, y) into the OT,
    // advancing the primitive pointer. Text is added at OT index 0 so it
    // draws on top of everything else.
    *pri = (char *)FntSort(ot, *pri, x, y, str);
}

void text_flush(void) {
    // No-op: FntSort adds primitives directly to the frame's OT.
}
