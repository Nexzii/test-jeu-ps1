#include "textures.h"
#include "engine3d.h"

/* Raw 16-bit BGR555 texture data, embedded via incbin (see CMakeLists.txt). */
extern const uint16_t tex_stone[], tex_brick[], tex_wood[],
                      tex_roof[], tex_grass[], tex_tree[],
                      tex_cloud[], tex_dialog[], tex_bg[];

uint16_t tp_stone, tp_brick, tp_wood, tp_roof, tp_grass, tp_tree, tp_cloud, tp_dialog, tp_bg;

void textures_load(void) {
    /* 128x128 world textures along the VRAM row at y=256 (all v=0 in page). */
    tp_stone = eng3d_load_texture(tex_stone, 128, 128, 320, 256);
    tp_brick = eng3d_load_texture(tex_brick, 128, 128, 448, 256);
    tp_wood  = eng3d_load_texture(tex_wood,  128, 128, 576, 256);
    tp_roof  = eng3d_load_texture(tex_roof,  128, 128, 704, 256);
    tp_tree  = eng3d_load_texture(tex_tree,  128, 128, 832, 256);

    /* Top row: battle bg (256x240), Cloud's 256x256 atlas, grass (128). */
    tp_bg    = eng3d_load_texture(tex_bg,    256, 240, 320, 0);
    tp_cloud = eng3d_load_texture(tex_cloud, 256, 256, 576, 0);
    tp_grass = eng3d_load_texture(tex_grass, 128, 128, 832, 0);

    /* Dialogue box skin at (320,384): page base y=256, so its UVs are v=128.. */
    tp_dialog = eng3d_load_texture(tex_dialog, 128, 128, 320, 384);
}
