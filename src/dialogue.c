#include "dialogue.h"
#include "text.h"
#include "textures.h"
#include <psxgpu.h>
#include <psxpad.h>

#define DLG_OT 16

static const char *const *dlg_lines;
static int dlg_count;
static int dlg_idx;
static int dlg_active;

void dialogue_open(const char *const *lines, int count) {
    dlg_lines  = lines;
    dlg_count  = count;
    dlg_idx    = 0;
    dlg_active = (count > 0);
}

void dialogue_update(int (*pressed)(uint16_t)) {
    if (!dlg_active) return;
    if (pressed(PAD_CROSS)) {
        dlg_idx++;
        if (dlg_idx >= dlg_count) dlg_active = 0;
    }
}

void dialogue_draw(uint32_t *ot, char **pri) {
    if (!dlg_active) return;

    int bx = 14, by = 162, bw = 292, bh = 64;

    /* Light cyan border behind the window. */
    TILE *brd = (TILE *)*pri;
    setTile(brd);
    setXY0(brd, bx - 3, by - 3);
    setWH(brd, bw + 6, bh + 6);
    setRGB0(brd, 120, 190, 230);
    addPrim(ot + DLG_OT + 1, brd);
    *pri += sizeof(TILE);

    /* Window fill = the blue-gradient region of the skin. In the skin sheet
     * that is the top-left 96x96; in our 128x128 texture that's the top-left
     * 64x64. The texture sits at VRAM y=384 (page base 256) so v is offset by
     * 128 -> sample u 0..63, v 128..191. */
    POLY_FT4 *box = (POLY_FT4 *)*pri;
    setPolyFT4(box);
    setXY4(box, bx, by,  bx + bw, by,  bx, by + bh,  bx + bw, by + bh);
    setUVWH(box, 0, 128, 63, 63);
    box->tpage = tp_dialog;
    box->clut = 0;
    setRGB0(box, 128, 128, 128);
    addPrim(ot + DLG_OT, box);
    *pri += sizeof(POLY_FT4);

    text_draw(dlg_lines[dlg_idx], 28, 182, ot, pri);
    text_draw("X=NEXT", 256, 210, ot, pri);   /* advance hint */
}

int dialogue_active(void) {
    return dlg_active;
}
